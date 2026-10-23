if (typeof window !== 'undefined') { window.__webgpuPowerPreference = 'high-performance'; }
if (typeof window === 'undefined' || !window.RenderEngine) {
// --- format.js ---
// Parse binary scene blob produced by Python serialize_scene()
//
// Layout:
//   [4 bytes] magic "WGPU"
//   [4 bytes] version (uint32 LE, = 1)
//   [4 bytes] JSON length N (uint32 LE)
//   [N bytes] JSON metadata (UTF-8)
//   [padding to 16-byte alignment from JSON start]
//   [remaining] concatenated binary data (each chunk 16-byte aligned)

function parseSceneBlob(arrayBuffer) {
  const view = new DataView(arrayBuffer);

  // Header
  const magic = String.fromCharCode(
    view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3)
  );
  if (magic !== 'WGPU') throw new Error('Invalid scene blob magic');
  const version = view.getUint32(4, true);
  if (version !== 1) throw new Error(`Unsupported version: ${version}`);
  const jsonLength = view.getUint32(8, true);

  // JSON metadata
  const jsonBytes = new Uint8Array(arrayBuffer, 12, jsonLength);
  const metadata = JSON.parse(new TextDecoder().decode(jsonBytes));

  // Binary data starts after 16-byte-aligned JSON
  const jsonPaddedSize = (jsonLength + 15) & ~15;
  const binaryStart = 12 + jsonPaddedSize;
  const binaryData = new Uint8Array(arrayBuffer, binaryStart);

  // Attach binary slices
  for (const buf of Object.values(metadata.buffers)) {
    buf.data = binaryData.slice(buf.offset, buf.offset + buf.byte_length);
  }
  for (const tex of Object.values(metadata.textures)) {
    tex.data = binaryData.slice(tex.offset, tex.offset + tex.byte_length);
  }

  return metadata;
}

{ parseSceneBlob };

// --- compute.js ---
// ---------------------------------------------------------------------------
// Helper: determine GPU resource type from id prefix
// ---------------------------------------------------------------------------

function resourceType(id) {
  if (id.startsWith('uniform_')) return 'buffer';
  if (id.startsWith('storage_')) return 'buffer';
  if (id.startsWith('tex_'))     return 'texture';
  if (id.startsWith('sampler_')) return 'sampler';
  if (id.startsWith('stex_'))    return 'storage-texture';
  return 'buffer'; // fallback
}

// ---------------------------------------------------------------------------
// Helper: build explicit GPUBindGroupLayout entries.
// For render passes, storage buffers are always read-only.
// For compute passes, parse shader to find read_write bindings.
// ---------------------------------------------------------------------------

function buildLayoutEntries(bindings, visibility, shader) {
  // For compute: find which bindings are read_write from shader declarations
  const rwBindings = new Set();
  if (shader) {
    const re = /@binding\((\d+)\)\s*var<storage\s*,\s*read_write>/g;
    let m;
    while ((m = re.exec(shader)) !== null) rwBindings.add(parseInt(m[1]));
  }

  const entries = [];
  for (const [bindingNum, resId] of Object.entries(bindings)) {
    const b = parseInt(bindingNum);
    const type = resourceType(resId);
    const entry = { binding: b, visibility };
    if (type === 'buffer') {
      if (resId.startsWith('uniform_')) {
        entry.buffer = { type: 'uniform' };
      } else if (rwBindings.has(b)) {
        entry.buffer = { type: 'storage' };
      } else {
        entry.buffer = { type: 'read-only-storage' };
      }
    } else if (type === 'texture') {
      entry.texture = { sampleType: 'float' };
    } else if (type === 'sampler') {
      entry.sampler = { type: 'filtering' };
    } else if (type === 'storage-texture') {
      entry.storageTexture = { access: 'write-only', format: 'rgba8unorm' };
    }
    entries.push(entry);
  }
  return entries;
}

// Built-in shader: cap the indirect instanceCount at the output buffer capacity.
// instanceCount lives at index 1 of both the 4-u32 drawIndirect layout and the
// 5-u32 drawIndexedIndirect layout, so a runtime-sized array handles both.
const CAP_INDIRECT_SHADER = `
@group(0) @binding(0) var<storage, read_write> counter: atomic<u32>;
@group(0) @binding(1) var<storage, read_write> indirect: array<u32>;
@group(0) @binding(2) var<uniform> max_instances: u32;

@compute @workgroup_size(1)
fn main() {
  let count = atomicLoad(&counter);
  indirect[1] = min(count, max_instances);
}
`;

class ComputeDAG {
  constructor() {
    this.passes = new Map(); // id → pass descriptor
    this.pipelines = new Map(); // id → GPUComputePipeline
    this.bindGroups = new Map(); // id → GPUBindGroup
    this.dirty = new Set(); // currently dirty trigger ids
    this.stagingBuffers = new Map(); // id → GPUBuffer for counter readback
    this._pendingReadbacks = [];
    this._readbackInFlight = false;
    this._capPipelines = new Map(); // id → { pipeline, bindGroup, maxBuf }
    this._executeGen = new Map(); // id → generation counter incremented each execute
    this._stagingGen = new Map(); // id → generation at time of staging copy
    this._resizedBufferIds = new Set(); // buffer IDs that JS resized (Python refs are stale)
  }

  addPass(id, { shader, bindings, workgroups, triggers, resetBuffers, indirectSetup, countThenFill, entryPoint }) {
    this.passes.set(id, { shader, bindings, workgroups, triggers: triggers || [], resetBuffers: resetBuffers || [], indirectSetup: indirectSetup || null, countThenFill: countThenFill || null, entryPoint: entryPoint || 'main' });
    this._checkCycles();
  }

  markDirty(triggerId) {
    this.dirty.add(triggerId);
  }

  execute(device, commandEncoder, buffers) {
    if (this.dirty.size === 0) return;

    // Collect triggered passes, cascading through the graph
    const triggered = new Set();
    const queue = [...this.dirty];
    while (queue.length > 0) {
      const tid = queue.shift();
      for (const [id, pass] of this.passes) {
        if (!triggered.has(id) && pass.triggers.includes(tid)) {
          triggered.add(id);
          queue.push(id); // this pass's id can trigger downstream
        }
      }
    }

    if (triggered.size === 0) {
      this.dirty.clear();
      return;
    }

    // Topo-sort triggered passes
    const sorted = this._topoSort(triggered);

    for (const id of sorted) {
      const pass = this.passes.get(id);
      const pipeline = this.pipelines.get(id);
      const bindGroup = this.bindGroups.get(id);
      if (!pipeline || !bindGroup) continue;  // pass skipped (pipeline build failed)

      // Bump generation — this pass has re-executed
      this._executeGen.set(id, (this._executeGen.get(id) || 0) + 1);

      // Clear reset buffers
      for (const bufId of pass.resetBuffers) {
        const buf = buffers.get(bufId);
        commandEncoder.clearBuffer(buf, 0, buf.size);
      }

      // Dispatch compute pass
      const computePass = commandEncoder.beginComputePass();
      computePass.setPipeline(pipeline);
      computePass.setBindGroup(0, bindGroup);
      const [x, y, z] = pass.workgroups;
      computePass.dispatchWorkgroups(x, y, z);
      computePass.end();

      // After dispatch: build drawIndirect args from atomic counter
      if (pass.indirectSetup) {
        const { counterId, indirectId, vertexCount } = pass.indirectSetup;
        const counterBuf = buffers.get(counterId);
        const indirectBuf = buffers.get(indirectId);
        device.queue.writeBuffer(indirectBuf, 0, new Uint32Array([vertexCount]));
        // Baseline: copy raw counter (fallback if cap shader unavailable)
        commandEncoder.copyBufferToBuffer(counterBuf, 0, indirectBuf, 4, 4);
        // Cap shader overwrites indirect[1] with min(counter, capacity)
        const cap = this._capPipelines.get(id + '_indirect');
        if (cap) {
          const capPass = commandEncoder.beginComputePass();
          capPass.setPipeline(cap.pipeline);
          capPass.setBindGroup(0, cap.bindGroup);
          capPass.dispatchWorkgroups(1);
          capPass.end();
        }
      }

      // countThenFill: set up indirect for current frame + copy counter to staging
      if (pass.countThenFill) {
        const { counterId, indirectId, vertexCount } = pass.countThenFill;
        if (indirectId) {
          const counterBuf = buffers.get(counterId);
          const indirectBuf = buffers.get(indirectId);
          device.queue.writeBuffer(indirectBuf, 0, new Uint32Array([vertexCount]));
          // Baseline: copy raw counter (fallback if cap shader unavailable)
          commandEncoder.copyBufferToBuffer(counterBuf, 0, indirectBuf, 4, 4);
          // Cap shader overwrites indirect[1] with min(counter, capacity)
          const cap = this._capPipelines.get(id + '_ctf');
          if (cap) {
            const capPass = commandEncoder.beginComputePass();
            capPass.setPipeline(cap.pipeline);
            capPass.setBindGroup(0, cap.bindGroup);
            capPass.dispatchWorkgroups(1);
            capPass.end();
          }
        }
        // Only copy to staging for resize readback if no readback is in flight,
        // otherwise the staging buffer is mapped/pending-map and the submit
        // would fail with "used in submit while mapped".
        if (!this._readbackInFlight) {
          const counterBuf = buffers.get(counterId);
          const staging = this.stagingBuffers.get(id);
          commandEncoder.copyBufferToBuffer(counterBuf, 0, staging, 0, 4);
          this._pendingReadbacks.push(id);
          this._stagingGen.set(id, this._executeGen.get(id));
        }
      }

      this.dirty.add(id);
    }

    this.dirty.clear();
  }

  async initPipelines(device, buffers) {
    // Create the shared cap-indirect pipeline (one shader for all passes)
    if (!this._capModule) {
      try {
        this._capModule = device.createShaderModule({ code: CAP_INDIRECT_SHADER });
        const capLayout = device.createBindGroupLayout({
          entries: [
            { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
            { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
          ],
        });
        this._capLayout = capLayout;
        this._capPipelineGPU = await device.createComputePipelineAsync({
          layout: device.createPipelineLayout({ bindGroupLayouts: [capLayout] }),
          compute: { module: this._capModule, entryPoint: 'main' },
        });
      } catch (e) {
        console.warn('[ComputeDAG] Cap-indirect pipeline failed, using fallback:', e);
        this._capModule = null;
      }
    }

    for (const [id, pass] of this.passes) {
      await this._initPass(device, buffers, id);
    }
  }

  /**
   * Build the pipeline, owned buffers, bind group, staging + cap state for a
   * single pass. Used by initPipelines() and by incremental sync (addPass of a
   * newly-toggled renderer) so a new pass can start running without tearing
   * down and rebuilding the whole DAG (which would discard other passes'
   * already-converged owned buffers and invalidate in-flight references).
   */
  async _initPass(device, buffers, id) {
    const pass = this.passes.get(id);
    if (!pass) return;
    const module = device.createShaderModule({ code: pass.shader });

    // Build explicit layout — parse shader for read_write bindings
    const vis = GPUShaderStage.COMPUTE;
    const layoutEntries = buildLayoutEntries(pass.bindings, vis, pass.shader);
    const bindGroupLayout = device.createBindGroupLayout({ entries: layoutEntries });
    const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] });

    let pipeline;
    try {
      pipeline = await device.createComputePipelineAsync({
        layout: pipelineLayout,
        compute: { module, entryPoint: pass.entryPoint || "main" },
      });
    } catch (e) {
      console.error(`[ComputeDAG] compute pipeline '${id}' (entry=${pass.entryPoint || 'main'}) failed:`, e.message || e);
      return;
    }
    this.pipelines.set(id, pipeline);

    // For countThenFill passes, the JS engine owns the output buffer.
    // Create it at minimal size BEFORE building the bind group so the
    // bind group references the JS-owned buffer (not Python's placeholder).
    if (pass.countThenFill) {
      const ctf = pass.countThenFill;
      // Output buffers may be consumed by a render pass as STORAGE (e.g.
      // clipping SubTrigs) or as VERTEX buffers (e.g. arrow instance arrays),
      // so include both usages — extra flags are harmless.
      const outUsage = GPUBufferUsage.STORAGE | GPUBufferUsage.VERTEX
        | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;
      const makeOwned = (bid, size) => {
        const buf = device.createBuffer({ size: Math.max(size, 4), usage: outUsage, label: bid });
        buffers.set(bid, buf);
        this._resizedBufferIds.add(bid);
      };
      // Pre-size to the producer's known element count when supplied, so a
      // static count-then-fill (e.g. surface arrows, toggled once) fills every
      // element on the first frame rather than relying on the async-readback
      // resize to converge over several frames. Defaults to the 1-element
      // bootstrap when no hint is given (the readback resize then grows it).
      const initElems = (ctf.initialCount && ctf.initialCount > 0) ? ctf.initialCount : 1;
      // Primary output (drives the instance-count cap).
      makeOwned(ctf.outputId, (ctf.elementSize || 64) * initElems);
      // Sibling outputs, resized in lockstep with the primary.
      for (const s of ctf.siblings || []) makeOwned(s.id, s.elementSize * initElems);

      // Also create the indirect buffer (JS-owned). Indexed draws use the
      // 5-u32 drawIndexedIndirect layout (20 bytes); non-indexed use 4 (16).
      if (ctf.indirectId) {
        const indirectBuf = device.createBuffer({
          size: ctf.indexed ? 20 : 16,
          usage: GPUBufferUsage.INDIRECT | GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
          label: ctf.indirectId,
        });
        buffers.set(ctf.indirectId, indirectBuf);
        this._resizedBufferIds.add(ctf.indirectId);
      }
    }

    const entries = Object.entries(pass.bindings).map(([binding, bufId]) => ({
      binding: parseInt(binding),
      resource: { buffer: buffers.get(bufId) },
    }));
    const bindGroup = device.createBindGroup({
      layout: bindGroupLayout,
      entries,
    });
    this.bindGroups.set(id, bindGroup);

    if (pass.countThenFill) {
      const ctf = pass.countThenFill;
      const staging = device.createBuffer({
        size: 4,
        usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
        label: `${id}_staging`,
      });
      this.stagingBuffers.set(id, staging);
      this._buildCapBindGroup(device, buffers, id, '_ctf', ctf);
    }
    if (pass.indirectSetup) {
      this._buildCapBindGroup(device, buffers, id, '_indirect', pass.indirectSetup);
    }
  }

  /**
   * Tear down a single pass (a renderer was toggled off). Leaves other passes
   * and the shared buffers map untouched. Does NOT destroy the pass's owned
   * GPU buffers — the host may still hold references during the same update —
   * and drops any queued readback so processReadbacks() won't touch its staging.
   */
  removePass(id) {
    this.passes.delete(id);
    this.pipelines.delete(id);
    this.bindGroups.delete(id);
    this.stagingBuffers.delete(id);
    this._capPipelines.delete(id + '_ctf');
    this._capPipelines.delete(id + '_indirect');
    this._executeGen.delete(id);
    this._stagingGen.delete(id);
    this._pendingReadbacks = this._pendingReadbacks.filter((p) => p !== id);
    this.dirty.delete(id);
  }

  /**
   * Full teardown for a discarded scene: destroy every GPU buffer this DAG
   * created. The countThenFill output/indirect buffers it resized live in the
   * shared `buffers` map (the engine's id→GPUBuffer map, passed in) and are
   * tracked in _resizedBufferIds; the staging and cap-uniform buffers are owned
   * here. Unlike removePass(), this DOES destroy them — the scene is going away,
   * so the host no longer holds live references.
   */
  dispose(buffers) {
    for (const buf of this.stagingBuffers.values()) {
      try { buf.destroy(); } catch (e) {}
    }
    for (const cap of this._capPipelines.values()) {
      if (cap && cap.maxBuf) { try { cap.maxBuf.destroy(); } catch (e) {} }
    }
    if (buffers) {
      for (const id of this._resizedBufferIds) {
        const buf = buffers.get(id);
        if (buf) { try { buf.destroy(); } catch (e) {} }
      }
    }
    this.stagingBuffers.clear();
    this._capPipelines.clear();
    this._resizedBufferIds.clear();
    this.passes.clear();
    this.pipelines.clear();
    this.bindGroups.clear();
  }

  _buildCapBindGroup(device, buffers, passId, suffix, setup) {
    const { counterId, indirectId, outputId, elementSize } = setup;
    const counterBuf = buffers.get(counterId);
    const indirectBuf = buffers.get(indirectId);
    // Compute max instances from output buffer size
    const outputBuf = outputId ? buffers.get(outputId) : null;
    const elemSize = elementSize || 64;
    const maxInstances = outputBuf ? Math.floor(outputBuf.size / elemSize) : 0xFFFFFFFF;

    // Destroy previous maxBuf if rebuilding
    const key = passId + suffix;
    const prev = this._capPipelines.get(key);
    if (prev && prev.maxBuf) prev.maxBuf.destroy();

    // Uniform buffer holding max_instances
    const maxBuf = device.createBuffer({
      size: 4,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      label: `${passId}${suffix}_cap_max`,
    });
    device.queue.writeBuffer(maxBuf, 0, new Uint32Array([maxInstances]));

    const bindGroup = device.createBindGroup({
      layout: this._capLayout,
      entries: [
        { binding: 0, resource: { buffer: counterBuf } },
        { binding: 1, resource: { buffer: indirectBuf } },
        { binding: 2, resource: { buffer: maxBuf } },
      ],
    });
    this._capPipelines.set(key, { pipeline: this._capPipelineGPU, bindGroup, maxBuf });
  }

  async processReadbacks(device, buffers, onRerender) {
    if (this._pendingReadbacks.length === 0) return false;
    if (this._readbackInFlight) return false;

    const passIds = [...this._pendingReadbacks];
    this._pendingReadbacks = [];
    this._readbackInFlight = true;

    let needsRerender = false;

    try {
      for (const id of passIds) {
        const pass = this.passes.get(id);
        const ctf = pass.countThenFill;
        const staging = this.stagingBuffers.get(id);

        await staging.mapAsync(GPUMapMode.READ);
        const count = new Uint32Array(staging.getMappedRange())[0];
        staging.unmap();

        const neededSize = Math.max(64, count * ctf.elementSize);
        const currentBuf = buffers.get(ctf.outputId);

        if (neededSize > currentBuf.size || neededSize < currentBuf.size / 4) {
          // Resize the primary and every sibling to the SAME element capacity,
          // so the shader's arrayLength(&primary) write-gate also bounds writes
          // into the siblings. Overallocate 2x when growing to provide headroom
          // for rapid changes (e.g. dragging the clipping slider); shrink to the
          // exact size. Capacity is in ELEMENTS (not bytes) so a 12-byte vec3
          // buffer and a 4-byte scalar buffer end up with matching element counts.
          const grow = neededSize > currentBuf.size;
          const cap = Math.max(16, grow ? count * 2 : count);
          const usage = GPUBufferUsage.STORAGE | GPUBufferUsage.VERTEX
            | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;
          const resizeOne = (bufId, elemSize) => {
            const newBuf = device.createBuffer({
              size: Math.max(cap * elemSize, 4), usage, label: bufId,
            });
            buffers.set(bufId, newBuf);
            // Track that JS resized this buffer — Python's reference is now stale
            // and engine.update() must not overwrite it.
            this._resizedBufferIds.add(bufId);
            this._rebuildBindGroups(device, buffers, bufId);
          };
          resizeOne(ctf.outputId, ctf.elementSize);
          for (const s of ctf.siblings || []) resizeOne(s.id, s.elementSize);
          // Rebuild cap bind group with updated max_instances (primary capacity)
          this._buildCapBindGroup(device, buffers, id, '_ctf', ctf);
          needsRerender = true;
        }

        if (ctf.indirectId) {
          // Only write the readback count to indirect if no newer compute pass
          // has executed since the staging copy was made.  If a newer pass ran,
          // its cap shader already set the correct (current) indirect count;
          // overwriting it here with a stale value would cause old triangles.
          const staleGen = this._stagingGen.get(id) || 0;
          const curGen = this._executeGen.get(id) || 0;
          if (staleGen >= curGen) {
            const indirectBuf = buffers.get(ctf.indirectId);
            // If we just resized, the output buffer is freshly allocated and
            // EMPTY — the fill runs on the next frame (onRerender re-marks the
            // trigger). Drawing `count` instances now would read uninitialized
            // slots → garbage positions/directions (the random-vectors / jump
            // bug). Draw 0 until the refill; the next execute's cap shader sets
            // the real count. When no resize happened the buffer already holds
            // `count` filled instances, so write it.
            const instanceCount = needsRerender ? 0 : count;
            // Indexed: [indexCount, instanceCount, firstIndex, baseVertex, firstInstance]
            // Non-indexed: [vertexCount, instanceCount, firstVertex, firstInstance]
            const args = ctf.indexed
              ? [ctf.vertexCount, instanceCount, 0, 0, 0]
              : [ctf.vertexCount, instanceCount, 0, 0];
            device.queue.writeBuffer(indirectBuf, 0, new Uint32Array(args));
          }
        }
      }
    } catch (e) {
      // mapAsync can fail if the device is lost or the buffer was destroyed.
      // Don't leave _readbackInFlight stuck — that would prevent all future
      // staging copies and buffer resizing, causing stale rendering.
      console.warn('[ComputeDAG] processReadbacks failed:', e.message || e);
    }

    this._readbackInFlight = false;

    if (needsRerender) {
      for (const id of passIds) {
        const pass = this.passes.get(id);
        for (const t of pass.triggers) this.markDirty(t);
      }
      onRerender();
    }
    // Report whether a resize happened so a synchronous driver (settle()) knows
    // the count-then-fill hasn't converged yet and must render again.
    return needsRerender;
  }

  _rebuildBindGroups(device, buffers, changedBufferId) {
    for (const [id, pass] of this.passes) {
      const usesBuffer = Object.values(pass.bindings).includes(changedBufferId);
      if (!usesBuffer) continue;

      const pipeline = this.pipelines.get(id);
      const layout = pipeline.getBindGroupLayout(0);
      const entries = Object.entries(pass.bindings).map(([binding, bufId]) => ({
        binding: parseInt(binding),
        resource: { buffer: buffers.get(bufId) },
      }));
      this.bindGroups.set(id, device.createBindGroup({ layout, entries }));
    }
  }

  // Build adjacency: if pass B has trigger "A" and A is a pass, then A → B
  _buildGraph() {
    const adj = new Map();
    const passIds = new Set(this.passes.keys());
    for (const id of passIds) adj.set(id, []);
    for (const [id, pass] of this.passes) {
      for (const t of pass.triggers) {
        if (passIds.has(t)) {
          adj.get(t).push(id);
        }
      }
    }
    return adj;
  }

  _checkCycles() {
    const adj = this._buildGraph();
    const WHITE = 0, GRAY = 1, BLACK = 2;
    const color = new Map();
    for (const id of adj.keys()) color.set(id, WHITE);

    const dfs = (u) => {
      color.set(u, GRAY);
      for (const v of adj.get(u)) {
        if (color.get(v) === GRAY) throw new Error(`ComputeDAG: cycle detected involving "${u}" and "${v}"`);
        if (color.get(v) === WHITE) dfs(v);
      }
      color.set(u, BLACK);
    };

    for (const id of adj.keys()) {
      if (color.get(id) === WHITE) dfs(id);
    }
  }

  _topoSort(triggered) {
    const adj = this._buildGraph();
    // Restrict to triggered passes
    const inDeg = new Map();
    for (const id of triggered) inDeg.set(id, 0);
    for (const id of triggered) {
      for (const v of adj.get(id)) {
        if (triggered.has(v)) {
          inDeg.set(v, inDeg.get(v) + 1);
        }
      }
    }

    const queue = [];
    for (const [id, deg] of inDeg) {
      if (deg === 0) queue.push(id);
    }

    const sorted = [];
    while (queue.length > 0) {
      const u = queue.shift();
      sorted.push(u);
      for (const v of adj.get(u)) {
        if (!triggered.has(v)) continue;
        const d = inDeg.get(v) - 1;
        inDeg.set(v, d);
        if (d === 0) queue.push(v);
      }
    }
    return sorted;
  }
}

{ ComputeDAG };

// --- camera.js ---
// Camera and Transform for WebGPU render engine
// Port of webgpu/camera.py — produces byte-identical uniform buffers

const DEG2RAD = Math.PI / 180;
const UNIFORM_BUFFER_SIZE = 336;

// ---------------------------------------------------------------------------
// 4x4 matrix helpers — row-major Float64Array[16], index = row*4 + col
// ---------------------------------------------------------------------------

function mat4Identity() {
  const m = new Float64Array(16);
  m[0] = m[5] = m[10] = m[15] = 1;
  return m;
}

function mat4Mul(a, b) {
  const o = new Float64Array(16);
  for (let i = 0; i < 4; i++)
    for (let j = 0; j < 4; j++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j];
      o[i * 4 + j] = s;
    }
  return o;
}

function mat4Inv(m) {
  // Gauss-Jordan elimination with partial pivoting
  const a = new Float64Array(m);
  const inv = mat4Identity();
  for (let col = 0; col < 4; col++) {
    let maxVal = Math.abs(a[col * 4 + col]);
    let maxRow = col;
    for (let row = col + 1; row < 4; row++) {
      const v = Math.abs(a[row * 4 + col]);
      if (v > maxVal) { maxVal = v; maxRow = row; }
    }
    if (maxRow !== col) {
      for (let j = 0; j < 4; j++) {
        let tmp;
        tmp = a[col * 4 + j]; a[col * 4 + j] = a[maxRow * 4 + j]; a[maxRow * 4 + j] = tmp;
        tmp = inv[col * 4 + j]; inv[col * 4 + j] = inv[maxRow * 4 + j]; inv[maxRow * 4 + j] = tmp;
      }
    }
    const diag = a[col * 4 + col];
    for (let j = 0; j < 4; j++) {
      a[col * 4 + j] /= diag;
      inv[col * 4 + j] /= diag;
    }
    for (let row = 0; row < 4; row++) {
      if (row === col) continue;
      const f = a[row * 4 + col];
      for (let j = 0; j < 4; j++) {
        a[row * 4 + j] -= f * a[col * 4 + j];
        inv[row * 4 + j] -= f * inv[col * 4 + j];
      }
    }
  }
  return inv;
}

// ---------------------------------------------------------------------------
// Transform — mirrors Python Transform exactly
// ---------------------------------------------------------------------------

class Transform {
  constructor() {
    this._mat = mat4Identity();
    this._center = [0, 0, 0];
  }

  copy() {
    const t = new Transform();
    t._mat = new Float64Array(this._mat);
    t._center = [...this._center];
    return t;
  }

  init(pmin, pmax) {
    const center = [
      0.5 * (pmin[0] + pmax[0]),
      0.5 * (pmin[1] + pmax[1]),
      0.5 * (pmin[2] + pmax[2]),
    ];
    this._center = center;
    const dx = pmax[0] - pmin[0];
    const dy = pmax[1] - pmin[1];
    const dz = pmax[2] - pmin[2];
    const scale = 2 / Math.sqrt(dx * dx + dy * dy + dz * dz);
    this._mat = mat4Identity();
    this.translate(-center[0], -center[1], -center[2]);
    this.scale(scale);
    if (!(Math.abs(pmin[2]) < 1e-12 && Math.abs(pmax[2]) < 1e-12)) {
      this.rotate(270, 0);
      this.rotate(0, -20);
      this.rotate(20, 0);
    }
  }

  translate(dx = 0, dy = 0, dz = 0) {
    if (Array.isArray(dx)) { [dx, dy, dz] = dx; }
    const t = mat4Identity();
    t[3] = dx; t[7] = dy; t[11] = dz;
    this._mat = mat4Mul(t, this._mat);
  }

  scale(s, center = null) {
    this._withCentering(center, () => {
      const sc = new Float64Array(16);
      sc[0] = s; sc[5] = s; sc[10] = s; sc[15] = 1;
      this._mat = mat4Mul(sc, this._mat);
    });
  }

  rotate(angX, angY = 0, center = null) {
    const rx = angX * DEG2RAD;
    const cx = Math.cos(rx), sx = Math.sin(rx);
    const rotX = mat4Identity();
    rotX[5] = cx; rotX[6] = -sx;
    rotX[9] = sx; rotX[10] = cx;

    const ry = angY * DEG2RAD;
    const cy = Math.cos(ry), sy = Math.sin(ry);
    const rotY = mat4Identity();
    rotY[0] = cy; rotY[2] = sy;
    rotY[8] = -sy; rotY[10] = cy;

    this._withCentering(center, () => {
      this._mat = mat4Mul(rotX, mat4Mul(rotY, this._mat));
    });
  }

  setCenter(center) {
    const mapped = this.mapPoint(center);
    this.translate(-mapped[0], -mapped[1], -mapped[2]);
    this._center = [...center];
  }

  resetXY(flip = false) {
    const s = this._currentScale();
    this._mat = mat4Identity();
    this.translate(-this._center[0], -this._center[1], -this._center[2]);
    this.scale(s);
    if (flip) this.rotate(0, 180);
  }

  resetXZ(flip = false) {
    this.resetXY();
    this.rotate(-90, 0);
    if (flip) this.rotate(0, 180);
  }

  resetYZ(flip = false) {
    this.resetXY();
    this.rotate(-90, 0);
    this.rotate(0, -90);
    if (flip) this.rotate(0, 180);
  }

  mapPoint(point) {
    const m = this._mat;
    const x = point[0], y = point[1], z = point[2];
    const w = m[12] * x + m[13] * y + m[14] * z + m[15];
    return [
      (m[0] * x + m[1] * y + m[2] * z + m[3]) / w,
      (m[4] * x + m[5] * y + m[6] * z + m[7]) / w,
      (m[8] * x + m[9] * y + m[10] * z + m[11]) / w,
    ];
  }

  get mat() { return this._mat; }

  _currentScale() {
    const m = this._mat;
    // Norm of column 0 of upper-left 3x3 (row-major: col 0 = m[0], m[4], m[8])
    return Math.sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
  }

  _withCentering(center, fn) {
    const c = center != null ? center : this._center;
    const mapped = this.mapPoint(c);
    this.translate(-mapped[0], -mapped[1], -mapped[2]);
    fn();
    this.translate(mapped[0], mapped[1], mapped[2]);
  }
}

// ---------------------------------------------------------------------------
// Camera — wraps Transform, provides uniform buffer and observer pattern
// ---------------------------------------------------------------------------

class Camera {
  constructor() {
    this.transform = new Transform();
    this.orthographic = false;
    this._observers = [];
  }

  reset(pmin, pmax) {
    this.transform.init(pmin, pmax);
    this._notify();
  }

  resetXY(flip = false) { this.transform.resetXY(flip); this._notify(); }
  resetXZ(flip = false) { this.transform.resetXZ(flip); this._notify(); }
  resetYZ(flip = false) { this.transform.resetYZ(flip); this._notify(); }

  registerObserver(cb) {
    if (!this._observers.includes(cb)) this._observers.push(cb);
  }

  unregisterObserver(cb) {
    this._observers = this._observers.filter(c => c !== cb);
  }

  _notify() {
    for (const cb of this._observers) cb();
  }

  /**
   * Compute camera uniform buffer matching CameraUniforms layout (336 bytes).
   *
   * Struct layout (all mat4 are 16 floats = 64 bytes):
   *   view                  [0..63]    column-major
   *   model_view            [64..127]  column-major
   *   model_view_projection [128..191] column-major
   *   rot_mat               [192..255] column-major
   *   normal_mat            [256..319] row-major (shader reads as inverse-transpose)
   *   aspect  (f32)         [320..323]
   *   width   (u32)         [324..327]
   *   height  (u32)         [328..331]
   *   dpr     (f32)         [332..335]  device-pixel ratio (0 in old blobs)
   */
  updateUniforms(width, height) {
    if (height === 0) return null;

    // --- Projection (matches Python camera.py exactly) ---
    const near = 0.1, far = 10, fov = 45, focal = 3;
    const aspect = width / height;
    const zoom = 1.0;

    const proj = new Float64Array(16);
    if (this.orthographic) {
      // Parallel projection: half-height matches perspective at the focal plane.
      const top = focal * Math.tan(fov * DEG2RAD / 2) * zoom;
      const h = 2 * top;
      const w = aspect * h;
      proj[0] = 2 / w;
      proj[5] = 2 / h;
      proj[10] = -1 / (far - near);
      proj[11] = -near / (far - near);
      proj[15] = 1;
    } else {
      const top = near * Math.tan(fov * DEG2RAD / 2) * zoom;
      const h = 2 * top;
      const w = aspect * h;
      const left = -0.5 * w;
      const right = left + w;
      const bottom = top - h;

      const px = 2 * near / (right - left);
      const py = 2 * near / (top - bottom);
      const a = (right + left) / (right - left);
      const b = (top + bottom) / (top - bottom);
      const c = -far / (far - near);
      const d = (-far * near) / (far - near);

      proj[0] = px; proj[2] = a;
      proj[5] = py; proj[6] = b;
      proj[10] = c; proj[11] = d;
      proj[14] = -1;
    }

    // --- View matrix: identity with z-translation = -3 ---
    const view = mat4Identity();
    view[11] = -3;

    const modelView = mat4Mul(view, this.transform.mat);
    const modelViewProj = mat4Mul(proj, modelView);
    const normalMat = mat4Inv(modelView);

    // --- Rotation matrix: normalized columns of upper-left 3x3 of modelView ---
    const rotMat = mat4Identity();
    for (let j = 0; j < 3; j++) {
      let norm = 0;
      for (let i = 0; i < 3; i++) norm += modelView[i * 4 + j] ** 2;
      norm = Math.sqrt(norm) || 1;
      for (let i = 0; i < 3; i++) rotMat[i * 4 + j] = modelView[i * 4 + j] / norm;
    }

    // --- Write to buffer ---
    const buf = new ArrayBuffer(UNIFORM_BUFFER_SIZE);
    const f32 = new Float32Array(buf);
    const u32 = new Uint32Array(buf);

    // Column-major: transpose row-major → f32[offset + col*4 + row]
    const writeColMajor = (offset, m) => {
      for (let i = 0; i < 4; i++)
        for (let j = 0; j < 4; j++)
          f32[offset + j * 4 + i] = m[i * 4 + j];
    };

    // Row-major: direct copy
    const writeRowMajor = (offset, m) => {
      for (let k = 0; k < 16; k++) f32[offset + k] = m[k];
    };

    writeColMajor(0, view);            // view
    writeColMajor(16, modelView);       // model_view
    writeColMajor(32, modelViewProj);   // model_view_projection
    writeColMajor(48, rotMat);          // rot_mat
    writeRowMajor(64, normalMat);       // normal_mat (row-major = col-major of transpose)

    f32[80] = aspect;
    u32[81] = width;
    u32[82] = height;
    f32[83] = (typeof window !== 'undefined' && window.devicePixelRatio) || 1;

    return buf;
  }
}

{ Camera, Transform, UNIFORM_BUFFER_SIZE };

// --- input.js ---
// Input handling for WebGPU canvas
// Port of camera mouse handling from webgpu/camera.py

class InputHandler {
  /**
   * @param {HTMLCanvasElement} canvas
   * @param {import('./camera.js').Camera} camera
   * @param {() => void} onRender — called after every camera change
   * @param {((x: number, y: number) => number[]|null)|null} getPositionFn — for dblclick center
   */
  constructor(canvas, camera, onRender, getPositionFn = null) {
    this.canvas = canvas;
    this.camera = camera;
    this.onRender = onRender;
    this.getPositionFn = getPositionFn;

    // Live mode: sink for classified non-camera events forwarded to the host.
    this._eventSink = null;
    // High-frequency move events (drag/hover) use true backpressure: at most one
    // is in flight to the host (Python) at a time. While one is being processed,
    // further moves are coalesced into a single pending event (deltas summed so
    // the rotation/pan total is preserved); when the host acks, the latest
    // pending move is sent. This caps the event rate to the host's actual
    // processing rate, so a fast drag can't pile up a backlog that replays slowly.
    this._pendingMove = null;
    this._moveInFlight = false;
    this._moveWatchdog = null;
    this.onGestureEnd = null;
    this.rotateSensitivity = 0.5;

    this._downButton = 0;
    this._downX = 0;
    this._downY = 0;
    this._moved = false;

    this._isRotating = false;
    this._isPanning = false;

    // Multi-touch gesture state
    this._activeTouches = new Map(); // id -> {x, y}
    this._prevTouchDist = null;
    this._prevTouchAngle = null;
    this._prevTouchCentroid = null;

    // Prevent browser scroll/zoom on the canvas
    canvas.style.touchAction = 'none';

    // Bind handlers for clean removal
    this._onPointerDown = this._onPointerDown.bind(this);
    this._onPointerUp = this._onPointerUp.bind(this);
    this._onPointerMove = this._onPointerMove.bind(this);
    this._onPointerLeave = this._onPointerLeave.bind(this);
    this._onWheel = this._onWheel.bind(this);
    this._onDblClick = this._onDblClick.bind(this);
    this._onContextMenu = (ev) => ev.preventDefault();
    this._onTouchStart = this._onTouchStart.bind(this);
    this._onTouchMove = this._onTouchMove.bind(this);
    this._onTouchEnd = this._onTouchEnd.bind(this);

    canvas.addEventListener('pointerdown', this._onPointerDown);
    canvas.addEventListener('pointerup', this._onPointerUp);
    canvas.addEventListener('pointermove', this._onPointerMove);
    canvas.addEventListener('pointerleave', this._onPointerLeave);
    canvas.addEventListener('wheel', this._onWheel, { passive: false });
    canvas.addEventListener('dblclick', this._onDblClick);
    canvas.addEventListener('contextmenu', this._onContextMenu);
    canvas.addEventListener('touchstart', this._onTouchStart, { passive: false });
    canvas.addEventListener('touchmove', this._onTouchMove, { passive: false });
    canvas.addEventListener('touchend', this._onTouchEnd);
    canvas.addEventListener('touchcancel', this._onTouchEnd);
  }

  setEventSink(fn) {
    this._eventSink = fn;
  }

  _buildPayload(type, ev) {
    const rect = this.canvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    return {
      type,
      button: ev.button == null ? 0 : ev.button,
      buttons: ev.buttons == null ? 0 : ev.buttons,
      x: ev.clientX,
      y: ev.clientY,
      canvasX: Math.round((ev.clientX - rect.left) * dpr),
      canvasY: Math.round((ev.clientY - rect.top) * dpr),
      movementX: ev.movementX || 0,
      movementY: ev.movementY || 0,
      deltaX: ev.deltaX || 0,
      deltaY: ev.deltaY || 0,
      ctrlKey: !!ev.ctrlKey,
      shiftKey: !!ev.shiftKey,
      altKey: !!ev.altKey,
    };
  }

  _send(payload) {
    try {
      this._eventSink(payload);
    } catch (e) {
      console.warn('[input] event sink failed:', e && (e.message || e));
    }
  }

  // Discrete events (click, dblclick, wheel, mouseout): flush any pending
  // coalesced move first so ordering is preserved, then send immediately.
  _forward(type, ev) {
    if (!this._eventSink) return;
    this._flushMove();
    this._send(this._buildPayload(type, ev));
  }

  // High-frequency move events (drag/hover): backpressured. Accumulate deltas
  // into a single pending event while one is in flight; send it once the host
  // acks the previous one.
  _forwardMove(type, ev) {
    if (!this._eventSink) return;
    const payload = this._buildPayload(type, ev);
    const prev = this._pendingMove;
    if (prev && prev.type === type) {
      payload.movementX += prev.movementX;
      payload.movementY += prev.movementY;
    }
    this._pendingMove = payload;
    this._flushMove();
  }

  _flushMove() {
    if (this._moveInFlight) return;          // wait for the host's ack
    const p = this._pendingMove;
    if (!p) return;
    this._pendingMove = null;
    this._moveInFlight = true;
    this._send(p);
    // Watchdog: the host acks via ackInput() once it has processed the event.
    // If that ack is ever lost, don't stall input forever.
    if (typeof setTimeout !== 'undefined') {
      this._moveWatchdog = setTimeout(() => this._releaseMove(), 1000);
    }
  }

  // Called (via engine.ackInput) once the host finished the in-flight move.
  _releaseMove() {
    if (!this._moveInFlight) return;
    this._moveInFlight = false;
    if (this._moveWatchdog != null) {
      clearTimeout(this._moveWatchdog);
      this._moveWatchdog = null;
    }
    if (!this._pendingMove) return;
    // Defer so we never recurse synchronously when the host (Pyodide) acks
    // inline within the same call stack.
    if (typeof Promise !== 'undefined') {
      Promise.resolve().then(() => this._flushMove());
    } else {
      this._flushMove();
    }
  }

  // --- Pointer events (mouse & single touch fallback) ---

  _onPointerDown(ev) {
    ev.preventDefault();
    this._downButton = ev.button;
    this._downX = ev.clientX;
    this._downY = ev.clientY;
    this._moved = false;
    // ctrl/alt are reserved for the host and never start a camera gesture.
    const hostModified = ev.ctrlKey || ev.altKey;
    if (!hostModified && ev.button === 0 && !ev.shiftKey) {
      this._isRotating = true;
    } else if (!hostModified && (ev.button === 1 || (ev.button === 0 && ev.shiftKey))) {
      this._isPanning = true;
    }
    this.canvas.setPointerCapture(ev.pointerId);
  }

  _onPointerUp(ev) {
    const wasGesture = this._isRotating || this._isPanning;
    this._isRotating = false;
    this._isPanning = false;
    // Apply any pending coalesced drag before the gesture ends, so the final
    // pointer position isn't dropped.
    this._flushMove();
    // A press with no movement is a click; camera gestures set _moved.
    if (this._eventSink && !this._moved && ev.button === this._downButton) {
      this._forward('click', ev);
    }
    if (wasGesture && this._moved && this.onGestureEnd) {
      try { this.onGestureEnd(); } catch (e) { /* ignore */ }
    }
  }

  _onPointerMove(ev) {
    // Suppress pointer-based rotation/pan while a multi-touch gesture is active
    if (this._activeTouches.size >= 2) return;

    // On high-DPI screens (e.g. macOS Retina) the OS reports pointer movement
    // in device pixels, which are then divided by devicePixelRatio to produce
    // CSS-pixel deltas.  Multiply back by DPR so that the same physical finger
    // movement always produces the same camera rotation/pan, regardless of
    // the screen's pixel density.
    const dpr = window.devicePixelRatio || 1;
    const t = this.camera.transform;
    if (this._isRotating) {
      this._moved = true;
      const s = this.rotateSensitivity * dpr;
      t.rotate(s * ev.movementY, s * ev.movementX);
      this.camera._notify();
      this.onRender();
      return;
    }
    if (this._isPanning) {
      this._moved = true;
      t.translate(0.01 * dpr * ev.movementX, -0.01 * dpr * ev.movementY);
      this.camera._notify();
      this.onRender();
      return;
    }
    // Not a camera gesture — forward to the host (hover, or a modified drag).
    // Coalesced to one event per frame so a fast drag doesn't pile up a backlog.
    if (!this._eventSink) return;
    if (ev.buttons !== 0) {
      this._moved = true;
      this._forwardMove('drag', ev);
    } else {
      this._forwardMove('mousemove', ev);
    }
  }

  _onPointerLeave(ev) {
    if (this._eventSink) this._forward('mouseout', ev);
  }

  _onWheel(ev) {
    ev.preventDefault();
    if (this._eventSink && (ev.ctrlKey || ev.altKey)) {
      this._forward('wheel', ev);
      return;
    }
    const t = this.camera.transform;
    t.scale(1 - ev.deltaY / 1000, t._center);
    this.camera._notify();
    this.onRender();
  }

  _onDblClick(ev) {
    if (this._eventSink) this._forward('dblclick', ev);
    if (!this.getPositionFn) return;
    const rect = this.canvas.getBoundingClientRect();
    const x = ev.clientX - rect.left;
    const y = ev.clientY - rect.top;
    const p = this.getPositionFn(x, y);
    if (p != null) {
      this.camera.transform.setCenter(p);
      this.camera._notify();
      this.onRender();
    }
  }

  // --- Multi-touch gesture handling ---

  _onTouchStart(ev) {
    ev.preventDefault();
    for (const touch of ev.changedTouches) {
      this._activeTouches.set(touch.identifier, { x: touch.clientX, y: touch.clientY });
    }
    if (this._activeTouches.size >= 2) {
      this._initGestureState();
    }
  }

  _onTouchMove(ev) {
    ev.preventDefault();
    for (const touch of ev.changedTouches) {
      if (this._activeTouches.has(touch.identifier)) {
        this._activeTouches.set(touch.identifier, { x: touch.clientX, y: touch.clientY });
      }
    }
    if (this._activeTouches.size >= 2) {
      this._handleMultiTouchGesture();
    }
  }

  _onTouchEnd(ev) {
    for (const touch of ev.changedTouches) {
      this._activeTouches.delete(touch.identifier);
    }
    // Reset gesture state when fewer than 2 touches remain
    if (this._activeTouches.size < 2) {
      this._prevTouchDist = null;
      this._prevTouchAngle = null;
      this._prevTouchCentroid = null;
    }
  }

  /** Compute initial distance, angle, and centroid for a two-finger gesture. */
  _initGestureState() {
    const [a, b] = this._getTwoTouches();
    this._prevTouchDist = this._distance(a, b);
    this._prevTouchAngle = this._angle(a, b);
    this._prevTouchCentroid = this._centroid(a, b);
  }

  /** Process ongoing two-finger gesture: pinch-zoom, rotation, and pan. */
  _handleMultiTouchGesture() {
    const [a, b] = this._getTwoTouches();
    const dist = this._distance(a, b);
    const angle = this._angle(a, b);
    const centroid = this._centroid(a, b);
    const t = this.camera.transform;

    // Pinch-to-zoom
    if (this._prevTouchDist != null && this._prevTouchDist > 0) {
      const scaleFactor = dist / this._prevTouchDist;
      t.scale(scaleFactor, t._center);
    }

    // Two-finger rotation
    if (this._prevTouchAngle != null) {
      let angleDelta = angle - this._prevTouchAngle;
      // Normalize to [-PI, PI]
      if (angleDelta > Math.PI) angleDelta -= 2 * Math.PI;
      if (angleDelta < -Math.PI) angleDelta += 2 * Math.PI;
      const degrees = angleDelta * (180 / Math.PI);
      t.rotate(0, degrees);
    }

    // Two-finger pan (centroid movement)
    if (this._prevTouchCentroid != null) {
      const dx = centroid.x - this._prevTouchCentroid.x;
      const dy = centroid.y - this._prevTouchCentroid.y;
      t.translate(0.01 * dx, -0.01 * dy);
    }

    this._prevTouchDist = dist;
    this._prevTouchAngle = angle;
    this._prevTouchCentroid = centroid;

    this.camera._notify();
    this.onRender();
  }

  /** Return the first two active touch positions. */
  _getTwoTouches() {
    const iter = this._activeTouches.values();
    return [iter.next().value, iter.next().value];
  }

  _distance(a, b) {
    const dx = b.x - a.x;
    const dy = b.y - a.y;
    return Math.sqrt(dx * dx + dy * dy);
  }

  _angle(a, b) {
    return Math.atan2(b.y - a.y, b.x - a.x);
  }

  _centroid(a, b) {
    return { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
  }

  // --- Cleanup ---

  dispose() {
    this._pendingMove = null;
    this._moveInFlight = false;
    if (this._moveWatchdog != null) { clearTimeout(this._moveWatchdog); this._moveWatchdog = null; }
    const c = this.canvas;
    c.removeEventListener('pointerdown', this._onPointerDown);
    c.removeEventListener('pointerup', this._onPointerUp);
    c.removeEventListener('pointermove', this._onPointerMove);
    c.removeEventListener('pointerleave', this._onPointerLeave);
    c.removeEventListener('wheel', this._onWheel);
    c.removeEventListener('dblclick', this._onDblClick);
    c.removeEventListener('contextmenu', this._onContextMenu);
    c.removeEventListener('touchstart', this._onTouchStart);
    c.removeEventListener('touchmove', this._onTouchMove);
    c.removeEventListener('touchend', this._onTouchEnd);
    c.removeEventListener('touchcancel', this._onTouchEnd);
  }
}

{ InputHandler };

// --- interactions.js ---
// Interactions — GUI controls that update uniform buffers and trigger compute/render

/**
 * Get an existing child folder by title, or create a new one.
 * Prevents duplicate folders when multiple renderers the same interaction type.
 */
function getOrCreateFolder(gui, title, { open = false } = {}) {
  for (const f of gui.folders) {
    if (f._title === title) return f;
  }
  const folder = gui.addFolder(title);
  if (!open) folder.close();

  // Remove any 'Empty' placeholder leaf nodes that lil-gui inserts for empty folders.
  try {
    const el = folder.domElement;
    if (el) {
      // Remove any leaf element whose trimmed text content is exactly 'Empty'
      const leaves = el.querySelectorAll('*');
      for (const node of leaves) {
        if (node.childElementCount === 0 && node.textContent && node.textContent.trim() === 'Empty') {
          node.remove();
        }
      }
    }
  } catch (e) {
    // ignore
  }

  return folder;
}

async function loadLilGui() {
  if (window.lil && window.lil.GUI) return window.lil.GUI;
  if (window.createLilGUI) {
    // link.js provides createLilGUI — call it to ensure lil-gui is loaded
    const gui = await window.createLilGUI({ autoPlace: false });
    gui.destroy();
    return window.lil.GUI;
  }
  const url = 'https://cdn.jsdelivr.net/npm/lil-gui@0.20';
  if (window.define === undefined) {
    await import(url);
  } else {
    await new Promise((resolve) => {
      require([url], (module) => {
        window.lil = module;
        resolve();
      });
    });
  }
  return window.lil.GUI;
}

// ---------------------------------------------------------------------------
// Interaction handlers
// ---------------------------------------------------------------------------

const interactionHandlers = {};

// --- Time Animation ---
// config: { frames: [frame_buf_id_for_target_0, frame_buf_id_for_target_1, ...] }
// buffer_id: the live GPU buffer to overwrite when the slider moves.
// On change, copies the selected frame's bytes into the GPU buffer.

interactionHandlers.time_animation = function (engine, interaction, gui) {
  const cfg = interaction.config;
  const targets = cfg.targets || [
    { buffer_id: interaction.buffer_id, frames: cfg.frames || [] },
  ];
  const nFrames = targets.length > 0 ? targets[0].frames.length : 0;
  if (nFrames === 0) return;

  const state = { time: 0 };

  function applyFrame() {
    const i = Math.max(0, Math.min(nFrames - 1, Math.round(state.time)));
    for (const t of targets) {
      const buf = engine.buffers.get(t.buffer_id);
      const data = engine.frameBuffers.get(t.frames[i]);
      if (buf && data) {
        engine.device.queue.writeBuffer(buf, 0, data);
      }
    }
    engine.render();
  }

  const folder = getOrCreateFolder(gui, cfg.label || 'Animation', { open: !!cfg.open });
  folder.add(state, 'time', 0, nFrames - 1, 1).name('Frame').onChange(applyFrame);

  // Apply initial frame so the canvas matches frame 0 on load.
  applyFrame();
};

// --- Generic GUI ---
// config: {
//   label: string,
//   vars: { name: defaultValue, ... },
//   controls: [{kind: 'checkbox'|'slider'|'dropdown', var, name, ...kindOpts}],
//   writes: [{
//     targets: [{buffer_id, offset, dtype: 'f32'|'u32'|'i32'}],
//     expr?: string,        // JS expr over vars + t (seconds since loop start)
//                           // May return a TypedArray/ArrayBuffer for bulk writes.
//     value?: any,          // constant (used if expr absent)
//     when?: string,        // JS expr; if falsy, skip
//     trigger?: string,     // var name or '*' (any); one-shot on change
//   }, ...]
// }
//
// Per-frame writes (no `trigger`) run inside a requestAnimationFrame loop
// while at least one of them has a truthy `when`. Trigger writes fire once
// whenever the named var changes via the GUI.

interactionHandlers.gui = function (engine, interaction, gui) {
  const cfg = interaction.config;
  const vars = { ...(cfg.vars || {}) };
  const controls = cfg.controls || [];
  const writes = cfg.writes || [];

  function compile(expr) {
    if (expr == null) return null;
    const keys = Object.keys(vars);
    const decl = keys.length
      ? `const {${keys.join(',')}} = __v;`
      : '';
    return new Function('__v', 't', `${decl} return (${expr});`);
  }

  const compiled = writes.map(w => ({
    targets: w.targets || [],
    exprFn: compile(w.expr),
    whenFn: compile(w.when),
    value: w.value,
    trigger: w.trigger,
  }));

  function dtypeArray(dtype, v) {
    if (dtype === 'u32') return new Uint32Array([v]);
    if (dtype === 'i32') return new Int32Array([v]);
    return new Float32Array([v]);
  }

  function executeWrite(w, t) {
    if (w.whenFn && !w.whenFn(vars, t)) return;
    const v = w.exprFn ? w.exprFn(vars, t) : w.value;
    if (v == null) return;
    for (const tgt of w.targets) {
      if (tgt.dtype === 'pass_enable') {
        const pass = engine.renderPassObjects.find(p => p.id === tgt.buffer_id);
        if (pass) pass.enabled = !!v;
        continue;
      }
      const buf = engine.buffers.get(tgt.buffer_id);
      if (!buf) continue;
      // If the expr returned a typed array or ArrayBuffer, write it directly.
      const data = (v.buffer instanceof ArrayBuffer || v instanceof ArrayBuffer)
        ? v : dtypeArray(tgt.dtype || 'f32', v);
      engine.device.queue.writeBuffer(buf, tgt.offset || 0, data);
      // Mark the buffer dirty so compute passes that depend on it re-run.
      if (engine.computeDAG) engine.computeDAG.markDirty(tgt.buffer_id);
    }
  }

  const perFrame = compiled.filter(w => !w.trigger);
  const triggers = compiled.filter(w => w.trigger);

  let raf = null;
  let t0 = 0;

  function needsLoop() {
    for (const w of perFrame) {
      if (!w.whenFn || w.whenFn(vars, 0)) return true;
    }
    return false;
  }

  function loop(now) {
    const t = (now - t0) / 1000;
    for (const w of perFrame) executeWrite(w, t);
    engine.render();
    raf = needsLoop() ? requestAnimationFrame(loop) : null;
  }

  function ensureLoop() {
    if (raf == null && needsLoop()) {
      t0 = performance.now();
      raf = requestAnimationFrame(loop);
    }
  }

  function onVarChange(varName) {
    for (const w of triggers) {
      if (w.trigger === '*' || w.trigger === varName) executeWrite(w, 0);
    }
    if (triggers.length) engine.render();
    ensureLoop();
  }

  const container = getOrCreateFolder(gui, cfg.label || 'Controls', { open: !!cfg.open });
  for (const c of controls) {
    const name = c.name || c.var;
    if (c.kind === 'checkbox') {
      container.add(vars, c.var).name(name).onChange(() => onVarChange(c.var));
    } else if (c.kind === 'slider') {
      container.add(vars, c.var, c.min, c.max, c.step || 0.01)
            .name(name).onChange(() => onVarChange(c.var));
    } else if (c.kind === 'dropdown') {
      container.add(vars, c.var, c.options).name(name)
            .onChange(() => onVarChange(c.var));
    }
  }
};

// ---------------------------------------------------------------------------

class Interactions {
  constructor(engine, guiContainer) {
    this.engine = engine;
    this.guiContainer = guiContainer;
    this.gui = null;
  }

  async setup(interactions) {
    if (!interactions || interactions.length === 0) return;

    const GUI = await loadLilGui();
    this.gui = new GUI({ container: this.guiContainer, autoPlace: !this.guiContainer, title: 'Controls' });
    this.gui.close();

    for (const interaction of interactions) {
      const handler = interactionHandlers[interaction.type];
      if (handler) {
        handler(this.engine, interaction, this.gui);
      } else {
        console.warn('Unknown interaction type:', interaction.type);
      }
    }
  }

  dispose() {
    if (this.gui) {
      this.gui.destroy();
      this.gui = null;
    }
  }
}

{ Interactions };

// --- engine.js ---
// RenderEngine — core WebGPU renderer for exported scene blobs.
// Depends on: parseSceneBlob (format.js), Camera (camera.js),
//             ComputeDAG (compute.js), InputHandler (input.js)
// All are concatenated into the same scope by the Python loader — no imports.

const SAMPLE_COUNT = 4;
const LIGHT_CLEAR_COLOR = Object.freeze({ r: 1.0, g: 1.0, b: 1.0, a: 1.0 });
const DARK_CLEAR_COLOR  = Object.freeze({ r: 0.8078, g: 0.8392, b: 0.8667, a: 1.0 });
const LIGHT_CANVAS_BG = '#ffffff';
const DARK_CANVAS_BG  = '#ced6dd';
const LIGHT_LABEL_COLOR = Object.freeze([0.0, 0.0, 0.0]);
const DARK_LABEL_COLOR  = Object.freeze([0.9, 0.9, 0.9]);
const DEPTH_FORMAT = 'depth24plus';
// Object-id / pick texture format. Matches Python canvas.select_format. The
// select pass renders into a host-owned texture of this format (non-MSAA).
const SELECT_FORMAT = 'rgba32uint';

const PREMULTIPLIED_BLEND = {
  color: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
  alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
};

const STRAIGHT_ALPHA_BLEND = {
  color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
  alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
};

function blendFor(rp) {
  if (rp.blend === 'straight') return STRAIGHT_ALPHA_BLEND;
  if (rp.blend === 'premultiplied') return PREMULTIPLIED_BLEND;
  // Blobs written before `blend` existed inferred it from depth_write.
  return rp.depth_write ? PREMULTIPLIED_BLEND : STRAIGHT_ALPHA_BLEND;
}

// ---------------------------------------------------------------------------
// Helper: buffer usage flags from usage string
// ---------------------------------------------------------------------------

function bufferUsageFlags(usage) {
  const U = GPUBufferUsage;
  switch (usage) {
    case 'uniform':       return U.UNIFORM | U.COPY_DST | U.COPY_SRC;
    case 'storage':       return U.STORAGE | U.COPY_DST | U.COPY_SRC;
    case 'storage-write': return U.STORAGE | U.COPY_DST | U.COPY_SRC;
    case 'vertex':        return U.VERTEX | U.COPY_DST;
    case 'index':         return U.INDEX | U.COPY_DST;
    case 'indirect':      return U.INDIRECT | U.STORAGE | U.COPY_DST | U.COPY_SRC;
    default:              return U.STORAGE | U.COPY_DST | U.COPY_SRC;
  }
}

// ---------------------------------------------------------------------------
// Helper: find the sampler resource ID that is co-located with a texture
//
// Convention: for a texture at binding N, the sampler is at binding N+1
// and its ID starts with "sampler_". We look it up in the bindings dict.
// ---------------------------------------------------------------------------

function findSamplerIdForTexture(bindings, textureBindingNum) {
  const nextBinding = String(textureBindingNum + 1);
  const id = bindings[nextBinding];
  if (id && id.startsWith('sampler_')) return { binding: textureBindingNum + 1, id };
  return null;
}

// ---------------------------------------------------------------------------
// Helper: draw-a-triangle init probe + adapter acquisition
//
// Some adapters accept requestDevice() but then fail to import the canvas
// swapchain image memory, producing errors that only surface once we actually
// render to the canvas, e.g.:
//   "Requested allocation size (946176) is smaller than the image requires
//    (1007616). at ImportMemory (.../vulkan/external_memory/...)"
//   "[Invalid Texture] is invalid due to a previous error."
// This is typically seen on a discrete "high-performance" GPU behind certain
// Vulkan drivers, while the integrated "low-power" adapter works fine. There
// is no way to fix the broken adapter, so the only reliable test is to really
// draw a triangle to the canvas and watch for device errors — if it fails we
// fall back to the other power preference and try again.
// ---------------------------------------------------------------------------

// Total time budget for one probe. Drawing a single triangle completes within
// a frame or two, and a broken adapter normally reports an error rather than
// stalling — but one that stalls anyway must not take startup down with it.
// Keep it short: this is time the user spends staring at nothing before we
// fall back to the other adapter.
const PROBE_TIMEOUT_MS = 750;

// Resolve to true if `promise` settles within `ms`, false if it times out.
// A rejection propagates so the caller's catch can classify it.
async function raceWithTimeout(promise, ms) {
  let timer;
  const timeout = new Promise((resolve) => { timer = setTimeout(() => resolve(false), ms); });
  try {
    return await Promise.race([promise.then(() => true), timeout]);
  } finally {
    clearTimeout(timer);
  }
}

const TRIANGLE_PROBE_WGSL = `
@vertex
fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(0.0, 0.5), vec2f(-0.5, -0.5), vec2f(0.5, -0.5));
  return vec4f(p[i], 0.0, 1.0);
}
@fragment
fn fs() -> @location(0) vec4f {
  return vec4f(0.0, 0.0, 0.0, 1.0);
}
`;

// Draw a single triangle to the canvas and return any device error it provoked.
// Resolves to an Error on failure, or null if the triangle rendered cleanly.
async function probeDeviceByDrawingTriangle(device, context, format) {
  // The swapchain-import failure can land in any error class, and may also
  // arrive asynchronously as an uncaptured error — watch for all of them.
  let captured = null;
  let settled = false;
  const trail = [];
  const deadline = Date.now() + PROBE_TIMEOUT_MS;
  const remaining = () => Math.max(0, deadline - Date.now());
  const onUncaptured = (e) => { captured = captured || e.error; };
  device.addEventListener('uncapturederror', onUncaptured);
  device.pushErrorScope('out-of-memory');
  device.pushErrorScope('internal');
  device.pushErrorScope('validation');
  try {
    const module = device.createShaderModule({ code: TRIANGLE_PROBE_WGSL });
    const pipeline = device.createRenderPipeline({
      layout: 'auto',
      vertex:   { module, entryPoint: 'vs' },
      fragment: { module, entryPoint: 'fs', targets: [{ format }] },
      primitive: { topology: 'triangle-list' },
    });
    // Render into the real canvas texture — this is what exercises the
    // swapchain image import that breaks on the bad adapter.
    const view = context.getCurrentTexture().createView();
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({
      colorAttachments: [{
        view,
        loadOp: 'clear',
        storeOp: 'store',
        clearValue: { r: 0, g: 0, b: 0, a: 1 },
      }],
    });
    pass.setPipeline(pipeline);
    pass.draw(3);
    pass.end();
    device.queue.submit([encoder.finish()]);
    // Bound the wait: an adapter that stalls here instead of reporting an
    // error would block startup indefinitely. Timing out means "no verdict",
    // which is not the same as blaming the adapter.
    trail.push('submitted');
    settled = await raceWithTimeout(device.queue.onSubmittedWorkDone(), remaining());
    trail.push(settled ? 'workDone' : 'workDone:timeout');
  } catch (e) {
    captured = captured || e;
    trail.push(`threw:${e.message || e}`);
  }
  // Pop scopes in reverse push order; keep the first non-null error. These are
  // awaits too, so they draw on the same budget.
  const scoped = [];
  for (let i = 0; i < 3; i++) {
    let popped = null;
    const done = await raceWithTimeout(
      device.popErrorScope().then((e) => { popped = e; }), remaining())
      .catch((e) => { popped = e; return true; });
    trail.push(done ? 'pop' : 'pop:timeout');
    scoped.push(popped);
  }
  device.removeEventListener('uncapturederror', onUncaptured);
  const err = captured || scoped.find(Boolean);
  if (err) {
    return { ok: false, inconclusive: !settled, trail,
             error: err instanceof Error ? err : new Error(err.message || String(err)) };
  }
  // No error, but the draw never confirmably completed: we cannot vouch for
  // this adapter, yet we have no reason to reject it either.
  return { ok: settled, inconclusive: !settled, error: null, trail };
}

// ---------------------------------------------------------------------------
// Power-preference persistence
//
// The preferred GPU adapter ('high-performance' vs 'low-power') is remembered
// across page loads in the browser's localStorage. Users can change it from the
// JS console via the global webgpuSetLowPower() / webgpuSetHighPerformance()
// helpers (exposed at the bottom of this file).
// ---------------------------------------------------------------------------

const WEBGPU_POWER_PREF_KEY = 'webgpuPowerPreference';

// Read the persisted choice, if any. Returns 'low-power', 'high-performance',
// or null when nothing valid is stored / localStorage is unavailable.
function storedPowerPreference() {
  try {
    if (typeof localStorage !== 'undefined') {
      const stored = localStorage.getItem(WEBGPU_POWER_PREF_KEY);
      if (stored === 'low-power' || stored === 'high-performance') return stored;
    }
  } catch (e) {
    // localStorage can throw (private mode, disabled cookies) — ignore.
  }
  return null;
}

// Write the power preference to localStorage (silently; no-op if unavailable).
function persistPowerPreference(pref) {
  try {
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem(WEBGPU_POWER_PREF_KEY, pref);
    }
  } catch (e) {
    console.warn('[engine] could not persist power preference:', e);
  }
}

// Resolve the GPU power preference to request. An explicit choice persisted in
// localStorage wins; otherwise fall back to the value injected by the host
// (window.__webgpuPowerPreference), then to 'high-performance'.
function resolvePowerPreference() {
  return storedPowerPreference()
    || (typeof window !== 'undefined' && window.__webgpuPowerPreference)
    || 'high-performance';
}

// Persist a power preference from the console helpers. Takes effect on reload.
function setPowerPreference(pref) {
  if (pref !== 'low-power' && pref !== 'high-performance') {
    throw new Error(`Invalid power preference "${pref}"`);
  }
  persistPowerPreference(pref);
  console.info(`[engine] WebGPU power preference set to "${pref}". Reload the page to apply.`);
}

function webgpuSetLowPower() { setPowerPreference('low-power'); }
function webgpuSetHighPerformance() { setPowerPreference('high-performance'); }

// Configure a canvas context on `device` and probe it. Split out from
// webgpuSelectDevice so the probe can also run against a device created
// elsewhere.
async function webgpuProbeDevice(device, canvas) {
  if (!navigator.gpu) return 'WebGPU not supported';
  const format = navigator.gpu.getPreferredCanvasFormat();
  const scratch = !canvas || !canvas.width || !canvas.height;
  const target = scratch ? document.createElement('canvas') : canvas;
  if (scratch) {
    target.width = 64;
    target.height = 64;
  }
  try {
    const context = target.getContext('webgpu');
    context.configure({
      device,
      format,
      alphaMode: 'premultiplied',
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    const res = await probeDeviceByDrawingTriangle(device, context, format);
    // Leave the host's canvas alone — it configures the context itself once a
    // device has been picked; only the throwaway one is torn down here.
    if (scratch) context.unconfigure();
    return { ok: res.ok, inconclusive: res.inconclusive,
             trail: (res.trail || []).join(','),
             error: res.error ? (res.error.message || String(res.error)) : null };
  } catch (e) {
    return { ok: false, inconclusive: false, trail: 'setup',
             error: e.message || String(e) };
  }
}

// ---------------------------------------------------------------------------
// The single device-selection entry point.
//
// Tries each power preference in turn, probing each candidate, and returns the
// first adapter that passes. Used by RenderEngine in the browser and by the
// Python host (webgpu.utils.init_device_sync) so both follow identical rules.
//
// `canvas` may be null — the probe then renders into a throwaway canvas.
// `explicit` pins a preference (e.g. from WEBGPU_POWER_PREFERENCE). A choice
// persisted in localStorage by an earlier successful probe is trusted the same
// way, so the probe only runs when we have nothing confirmed to go on.
// Returns { device, powerPreference, verdict, messages }, where verdict is
// 'probed' (confirmed good), 'unprobed' (probe skipped) or 'unverified'
// (no adapter could be confirmed; the preferred one is returned anyway).
// ---------------------------------------------------------------------------
async function webgpuSelectDevice(canvas, explicit) {
  if (!navigator.gpu) throw new Error('WebGPU not supported');

  const messages = [];
  // Probing means drawing into a canvas swapchain and waiting for the draw to
  // complete, so it needs a real canvas and a host that runs a frame loop. A
  // headless host (the docs exporter) says so via __webgpuHeadless: there the
  // probe can never reach a verdict, and merely attempting it leaves the device
  // with submitted work that never completes, wedging the next buffer readback.
  // In both cases pick by preference alone rather than testing.
  const headless = globalThis.__webgpuHeadless === true;
  const canProbe = !headless && !!(canvas && canvas.width && canvas.height);
  const trusted = explicit || storedPowerPreference() || (canProbe ? null : 'high-performance');
  const preferred = trusted || resolvePowerPreference();
  const order = [preferred].concat(
    ['high-performance', 'low-power'].filter((p) => p !== preferred));

  // Kept as a last resort when no adapter can be confirmed (e.g. the probe
  // itself cannot run) — a suspect device still beats no device at all.
  let unverified = null;
  let unverifiedPref = null;

  for (const powerPreference of order) {
    const adapter = await navigator.gpu.requestAdapter({ powerPreference });
    if (!adapter) {
      messages.push(`no WebGPU adapter for '${powerPreference}'`);
      continue;
    }
    let device;
    try {
      device = await adapter.requestDevice({
        requiredLimits: {
          maxBufferSize: adapter.limits.maxBufferSize,
          maxStorageBufferBindingSize: adapter.limits.maxStorageBufferBindingSize,
        },
      });
    } catch (e) {
      messages.push(`no WebGPU device for '${powerPreference}': ${e.message || e}`);
      continue;
    }
    device.lost.then((info) => {
      console.error(`[engine] device lost: ${info.reason}: ${info.message}`);
    });

    if (trusted) {
      if (!canProbe && !explicit) {
        messages.push(headless
          ? 'headless host, selecting by preference only'
          : 'no canvas to probe, selecting by preference only');
      }
      return { device, powerPreference, verdict: 'unprobed', messages };
    }

    const res = await webgpuProbeDevice(device, canvas);
    if (res.ok) {
      if (powerPreference !== preferred) {
        messages.push(`'${preferred}' adapter unusable, using '${powerPreference}'`);
      }
      persistPowerPreference(powerPreference);
      return { device, powerPreference, verdict: 'probed', messages };
    }
    if (res.inconclusive) {
      // The draw never confirmably finished, so there is no verdict — and no
      // grounds to blame this adapter. Whatever stalled it would stall the
      // other one just as long for the same non-answer, so stop here and keep
      // this device, without persisting a choice we never confirmed.
      messages.push(`probe inconclusive for '${powerPreference}' [${res.trail}]`
                    + (res.error ? `: ${res.error}` : ''));
      unverified = device;
      unverifiedPref = powerPreference;
      break;
    }
    messages.push(`'${powerPreference}' adapter failed the triangle probe [${res.trail}]: ${res.error}`);
    device.destroy();
  }

  if (unverified) {
    return { device: unverified, powerPreference: unverifiedPref,
             verdict: 'unverified', messages };
  }
  throw new Error(`No working WebGPU adapter (${messages.join('; ')})`);
}

// Acquire a working GPU device and configure the canvas context on it.
// Device selection itself lives in webgpuSelectDevice() so the browser and the
// Python host pick adapters by exactly the same rules.
// Returns { device, context, canvasFormat, powerPreference }.
async function acquireWebGpuDevice(canvas) {
  const format = navigator.gpu.getPreferredCanvasFormat();
  const { device, powerPreference, messages } = await webgpuSelectDevice(canvas, null);
  for (const m of messages) console.warn(`[engine] ${m}`);
  const context = canvas.getContext('webgpu');
  context.configure({
    device,
    format,
    alphaMode: 'premultiplied',
    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
  });
  return { device, context, canvasFormat: format, powerPreference };
}

// ---------------------------------------------------------------------------
// RenderEngine
// ---------------------------------------------------------------------------

class RenderEngine {

  /**
   * Create and initialize from a canvas element and a base64-encoded blob.
   */
  static async create(canvasId, base64Blob) {
    const binary = atob(base64Blob);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);

    const engine = new RenderEngine();
    await engine.init(document.getElementById(canvasId), bytes.buffer);
    return engine;
  }

  /**
   * Live entry point: bind the engine to GPU resources owned by an external
   * runtime (e.g. Pyodide). The descriptor provides already-allocated
   * GPUBuffer / GPUTexture / GPUSampler proxies.
   *
   * descriptor = {
   *   device: GPUDevice,
   *   context: GPUCanvasContext,           // already configure()d
   *   canvasFormat?: string,
   *   buffers:  Map<string, GPUBuffer>  | object,
   *   textures: Map<string, GPUTexture> | object,
   *   samplers: Map<string, GPUSampler> | object,
   *   render_passes:  ExportRenderPass[],
   *   compute_passes: ExportComputePass[],
   *   interactions?:  ExportInteraction[],
   *   camera?: { matrix?, center?, buffer_id? },
   *   light?:  { buffer_id?, data? },
   * }
   */
  static async createLive(canvas, descriptor) {
    try {
      const engine = new RenderEngine();
      await engine.initLive(canvas, descriptor);
      canvas.__engine = engine;
      return engine;
    } catch (err) {
      console.error('[engine] createLive failed:', err && (err.stack || err.message || err));
      throw err;
    }
  }

  async init(canvas, arrayBuffer) {
    this.canvas = canvas;
    this.mode = 'blob';

    // --- Parse blob ---
    this.scene = parseSceneBlob(arrayBuffer);

    // --- WebGPU device + canvas context ---
    // An explicit choice persisted in localStorage is trusted and skips the
    // probe; otherwise webgpuSelectDevice() probes each power preference in
    // turn and remembers the first one that really renders.
    const acquired = await acquireWebGpuDevice(canvas);
    this.device = acquired.device;
    this.context = acquired.context;
    this.canvasFormat = acquired.canvasFormat;

    // --- Create GPU resources ---
    this.buffers = new Map();   // id → GPUBuffer
    this.textures = new Map();  // id → GPUTexture
    this.samplers = new Map();  // id → GPUSampler (keyed by the sampler_ id)
    this.texViews = new Map();  // id → GPUTextureView
    this.frameBuffers = new Map(); // id → ArrayBuffer (CPU-only frame snapshots)

    this._createBuffers();
    this._createTextures();
    this._createDepthAndMSAA();

    // --- Camera ---
    this.camera = new Camera();
    if (this.scene.camera) {
      const c = this.scene.camera;
      if (c.matrix) this.camera.transform._mat = Float64Array.from(c.matrix);
      if (c.center) this.camera.transform._center = [...c.center];
    }
    this._updateCameraBuffer();

    // --- Light ---
    this._applyLight();

    await this._finishInit();
  }

  async initLive(canvas, descriptor) {
    this.canvas = canvas;
    this.mode = 'live';

    if (!descriptor.device)  throw new Error('createLive: descriptor.device required');
    if (!descriptor.context) throw new Error('createLive: descriptor.context required');

    this.device = descriptor.device;
    this.context = descriptor.context;
    this.canvasFormat = descriptor.canvasFormat
      || (navigator.gpu && navigator.gpu.getPreferredCanvasFormat())
      || 'bgra8unorm';

    // Synthesize a scene-shaped object so the rest of the engine stays generic.
    // No raw bytes — every GPU resource is supplied by the host.
    this.scene = {
      buffers: {},
      textures: {},
      compute_passes: descriptor.compute_passes || [],
      render_passes:  descriptor.render_passes  || [],
      interactions:   descriptor.interactions   || [],
      camera: descriptor.camera || {},
      light:  descriptor.light  || {},
      theme: descriptor.theme || {},
    };

    // Live-mode host callbacks (optional).
    this._onEvent = descriptor.on_event || null;
    this._onCameraChanged = descriptor.on_camera_changed || null;
    // Host clear color [r,g,b,a] overriding the built-in light/dark color.
    this._clearColorOverride = descriptor.clear_color || null;

    this.buffers  = _toMap(descriptor.buffers);
    this.textures = _toMap(descriptor.textures);
    this.samplers = new Map();
    this.texViews = new Map();
    this.frameBuffers = new Map();

    // Frame buffers (CPU-only blobs for animation snapshots)
    if (descriptor.frame_buffers) {
      for (const [id, data] of Object.entries(descriptor.frame_buffers)) {
        this.frameBuffers.set(id, data);
      }
    }

    for (const [id, buf] of this.buffers) {
      this.scene.buffers[id] = { usage: _usageFromId(id), size: buf.size };
    }
    for (const [id, tex] of this.textures) {
      this.scene.textures[id] = { format: tex.format, width: tex.width, height: tex.height };
      this.texViews.set(id, tex.createView());
    }

    // Sampler lookup is supplied directly by the host.
    this._samplerLookup = _toMap(descriptor.samplers || {});

    this._createDepthAndMSAA();

    // --- Camera ---
    this.camera = new Camera();
    if (this.scene.camera) {
      const c = this.scene.camera;
      if (c.matrix) this.camera.transform._mat = Float64Array.from(c.matrix);
      if (c.center) this.camera.transform._center = [...c.center];
    }
    this._updateCameraBuffer();

    // In live mode the host typically writes the light buffer directly.
    if (this.scene.light && this.scene.light.data) this._applyLight();

    await this._finishInit();
  }

  /** Translate a serialized compute pass (snake_case) into ComputeDAG.addPass args. */
  _translateComputePass(cp) {
    const indirectSetup = cp.indirect_setup ? {
      counterId: cp.indirect_setup.counter_id,
      indirectId: cp.indirect_setup.indirect_id,
      vertexCount: cp.indirect_setup.vertex_count,
    } : null;
    const countThenFill = cp.count_then_fill ? {
      counterId: cp.count_then_fill.counter_id,
      outputId: cp.count_then_fill.output_id,
      elementSize: cp.count_then_fill.element_size,
      indirectId: cp.count_then_fill.indirect_id || null,
      vertexCount: cp.count_then_fill.vertex_count || 0,
      // indexed: indirect buffer is the 5-u32 drawIndexedIndirect layout
      // (indexCount, instanceCount, firstIndex, baseVertex, firstInstance)
      // instead of the 4-u32 drawIndirect layout.
      indexed: !!cp.count_then_fill.indexed,
      initialCount: cp.count_then_fill.initial_count || 0,
      // siblings: extra output buffers resized in lockstep with the primary
      // from the same counter (e.g. a vector's positions/directions/values
      // arrays). Each: { id, elementSize }. The primary (outputId) still
      // drives the instanceCount cap.
      siblings: (cp.count_then_fill.siblings || []).map(s => ({
        id: s.id,
        elementSize: s.element_size,
      })),
    } : null;
    return {
      shader: cp.shader,
      bindings: this._intKeyBindings(cp.bindings),
      workgroups: cp.workgroups,
      triggers: cp.triggers,
      resetBuffers: cp.reset_buffers,
      entryPoint: cp.entry_point || 'main',
      indirectSetup,
      countThenFill,
    };
  }

  /** Build the compute DAG from scratch (first install). */
  async _buildComputeDAG() {
    this.computeDAG = new ComputeDAG();
    for (const cp of this.scene.compute_passes) {
      this.computeDAG.addPass(cp.id, this._translateComputePass(cp));
    }
    await this.computeDAG.initPipelines(this.device, this.buffers);
    for (const cp of this.scene.compute_passes) {
      for (const t of cp.triggers) this.computeDAG.markDirty(t);
    }
  }

  /**
   * Incrementally reconcile the compute DAG with this.scene.compute_passes
   * (called from update() when a renderer with a compute pass is toggled).
   * Adds newly-present passes and removes vanished ones, leaving UNCHANGED
   * passes — and their already-converged JS-owned buffers and in-flight
   * readbacks — completely untouched. A wholesale rebuild here instead would
   * discard those buffers and invalidate references the host/bridge still
   * holds (observed as "mapAsync: external Instance reference no longer exists").
   */
  async _syncComputeDAG() {
    if (!this.computeDAG) { await this._buildComputeDAG(); return; }
    const want = new Map(this.scene.compute_passes.map(cp => [cp.id, cp]));
    // Remove passes that are gone.
    for (const id of [...this.computeDAG.passes.keys()]) {
      if (!want.has(id)) this.computeDAG.removePass(id);
    }
    // Add passes that are new; init + trigger only those.
    for (const [id, cp] of want) {
      if (this.computeDAG.passes.has(id)) continue;
      this.computeDAG.addPass(id, this._translateComputePass(cp));
      await this.computeDAG._initPass(this.device, this.buffers, id);
      for (const t of cp.triggers) this.computeDAG.markDirty(t);
    }
  }

  async _finishInit() {
    const canvas = this.canvas;

    // --- Compute ---
    await this._buildComputeDAG();

    // --- Render pipelines ---
    this.renderPassObjects = [];
    await this._createRenderPipelines();

    // --- Input + camera ---
    // The JS engine owns the camera and input loop in both modes. In live mode
    // it also forwards non-camera events to the host and syncs settled camera
    // transforms back.
    this.camera.registerObserver(() => {
      this._updateCameraBuffer();
      if (this._cameraBufferId) {
        this.computeDAG.markDirty(this._cameraBufferId);
      }
      this.render();
      this._notifyCameraChanged();
    });
    this.input = new InputHandler(canvas, this.camera, () => this.render());
    if (this._onEvent) {
      this.input.setEventSink((ev) => {
        try {
          const r = this._onEvent(ev);
          if (r && typeof r.then === 'function') {
            r.catch((e) => console.warn('[engine] on_event failed:', e && (e.message || e)));
          }
        }
        catch (e) { console.warn('[engine] on_event failed:', e && (e.message || e)); }
      });
    }
    if (this._onCameraChanged) {
      this.input.onGestureEnd = () => this._notifyCameraChanged(true);
    }

    // --- Interactions (lil-gui) ---
    const guiContainerId = canvas.id.replace('canvas', 'lilgui');
    const guiContainer = document.getElementById(guiContainerId);
    this.interactions = new Interactions(this, guiContainer);
    if (this.scene.interactions && this.scene.interactions.length > 0) {
      try {
        await Promise.race([
          this.interactions.setup(this.scene.interactions),
          new Promise((_, reject) => setTimeout(() => reject(new Error('interactions setup timed out')), 5000)),
        ]);
      } catch (e) {
        console.warn('[engine] interactions setup failed:', e.message || e);
      }
    }

    // --- Resize handling ---
    // Live mode: the host owns the canvas and drives resize via its own
    // observer; it tells us when to react via engine.handleResize().
    if (this.mode !== 'live') {
      this._resizeObserver = new ResizeObserver(() => this._onResize());
      this._resizeObserver.observe(canvas);
    }

    // --- Theme handling ---
    this._applyTheme();
    this._setupThemeObserver();

    // --- First frame ---
    // In live mode, the host decides when to render. Don't auto-render.
    if (this.mode !== 'live') this.render();
  }

  /**
   * Capture the next rendered frame as a CPU-readable Uint8Array.
   * Resolves with { data, width, height, format } after the next render.
   * Used by screenshots and headless tests.
   */
  captureNextFrame() {
    return new Promise((resolve) => {
      (this._frameCaptureRequests = this._frameCaptureRequests || []).push(resolve);
      this._renderNow();
    });
  }

  /**
   * Capture the next rendered frame and return just the raw ArrayBuffer.
   * Unlike captureNextFrame(), the ArrayBuffer is returned at the top level
   * (not nested in an object) so it transfers correctly through the bridge.
   * Width/height/format should be read from the canvas separately.
   */
  /**
   * Drive count-then-fill passes to convergence. Each pass starts with a tiny
   * output buffer and grows it from a GPU-read atomic counter over several
   * frames; in the live GUI that happens across animation frames, but a one-shot
   * capture (screenshot, headless test) has no such loop — so we render and
   * await processReadbacks() in a loop here until no pass resizes any more. This
   * is what makes count-then-fill fields (e.g. clipping/surface vectors) appear
   * in screenshots and headless renders instead of coming out blank.
   */
  async settle(maxIterations = 32) {
    if (!this.computeDAG) return 0;
    // Force the count-then-fill passes to (re)run so they can converge.
    this.notifyDirty(null);
    let i = 0;
    for (; i < maxIterations; i++) {
      this._renderNow(null, /*autoReadback=*/false);
      const resized = await this.computeDAG.processReadbacks(
        this.device, this.buffers, () => this._rebuildRenderBindGroups()
      );
      if (!resized) break;
    }
    return i;
  }

  async captureFrameBuffer() {
    await this.settle();
    return new Promise((resolve) => {
      (this._frameCaptureRequests = this._frameCaptureRequests || []).push(
        ({ data }) => resolve(data)
      );
      this._renderNow();
    });
  }

  /**
   * Like captureFrameBuffer(), but renders at an arbitrary width×height
   * (e.g. for high-resolution screenshots) instead of the on-screen canvas
   * size. We render into a throwaway offscreen target with matching MSAA/depth
   * attachments and a camera aspect for width×height, then restore engine state
   * in finally(). The visible canvas (swapchain) is never touched, so the
   * on-screen view is unaffected.
   */
  async captureFrameBufferAt(width, height) {
    // Settle at the current size — count-then-fill is resolution-independent.
    await this.settle();

    const prevW = this.width, prevH = this.height;
    const prevDepth = this.depthTexture, prevMsaa = this.msaaTexture;

    const captureTex = this.device.createTexture({
      size: [width, height], format: this.canvasFormat,
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
      label: 'capture-custom',
    });
    // Size MSAA/depth + camera to width×height so the render pass is
    // self-consistent and _renderNow's reconciliation branch is a no-op
    // (its attachments already match the target size).
    this.width = width; this.height = height;
    this.depthTexture = this.device.createTexture({
      size: [width, height], format: DEPTH_FORMAT,
      usage: GPUTextureUsage.RENDER_ATTACHMENT, sampleCount: SAMPLE_COUNT,
      label: 'depth-capture',
    });
    this.msaaTexture = this.device.createTexture({
      size: [width, height], format: this.canvasFormat,
      usage: GPUTextureUsage.RENDER_ATTACHMENT, sampleCount: SAMPLE_COUNT,
      label: 'msaa-capture',
    });
    this._updateCameraBuffer(width, height);

    try {
      return await new Promise((resolve) => {
        (this._frameCaptureRequests = this._frameCaptureRequests || []).push(
          ({ data }) => resolve(data)
        );
        this._renderNow(captureTex);            // render to offscreen width×height target
      });
    } finally {
      captureTex.destroy();
      this.depthTexture.destroy(); this.msaaTexture.destroy();
      this.depthTexture = prevDepth; this.msaaTexture = prevMsaa;
      this.width = prevW; this.height = prevH;
      this._updateCameraBuffer();               // restore on-screen camera aspect
    }
  }

  _ensureCaptureTexture() {
    const w = this.width, h = this.height;
    if (!this._captureTex || this._captureTex.width !== w || this._captureTex.height !== h) {
      if (this._captureTex) this._captureTex.destroy();
      this._captureTex = this.device.createTexture({
        size: [w, h],
        format: this.canvasFormat,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
        label: 'capture-target',
      });
    }
    return this._captureTex;
  }

  enableHeadlessCapture() {
    this._headless = true;
    this._ensureCaptureTexture();
    return this._captureTex;
  }

  captureTexture() {
    return this._ensureCaptureTexture();
  }

  /**
   * Push updated render/compute pass descriptors and rebuild pipelines.
   * Used by the host (Python) when the renderer set or its options change.
   */
  async update({ render_passes, compute_passes, interactions, buffers, textures, samplers, frame_buffers, camera } = {}) {
    if (render_passes)  this.scene.render_passes  = render_passes;
    if (compute_passes) this.scene.compute_passes = compute_passes;
    if (interactions)   this.scene.interactions   = interactions;
    if (camera) {
      // The buffer registry can reassign the camera's string id when the
      // render-object set changes, so a cached _cameraBufferId would point at
      // whatever buffer now holds that id (e.g. the 4-byte "subdivision"
      // uniform). Refresh the camera and force _updateCameraBuffer to
      // re-resolve the id from the new descriptor.
      this.scene.camera = camera;
      this._cameraBufferId = (camera && camera.buffer_id) || null;
    }
    // Update live buffer/texture/sampler maps when the host recreates GPU resources.
    // Preserve buffers that the JS engine has resized via countThenFill —
    // Python doesn't know about JS-side resizes, so its references are stale
    // for those buffers. The JS engine is the authority for output buffer sizing.
    if (buffers) {
      const incoming = _toMap(buffers);
      const resized = this.computeDAG ? this.computeDAG._resizedBufferIds : new Set();
      // Rebuild this.buffers to exactly (engine-resized ∪ incoming host set).
      // Any id previously held that is neither is dropped, so the engine stops
      // referencing host buffers whose Python wrappers were freed — the host
      // then destroys them at its render-safe flush. Without this prune the map
      // grows forever and freed buffers would still be bound in a frame. We do
      // NOT destroy host buffers here (the host owns them); engine-owned
      // (resized) buffers are kept and freed in dispose().
      const next = new Map();
      for (const [id, buf] of this.buffers) {
        if (resized.has(id)) next.set(id, buf);
      }
      for (const [id, buf] of incoming) {
        if (!resized.has(id)) next.set(id, buf);
      }
      this.buffers = next;
    }
    if (textures) {
      this.textures = _toMap(textures);
      this.texViews = new Map();
      for (const [id, tex] of this.textures) {
        this.texViews.set(id, tex.createView());
      }
    }
    if (samplers) {
      this._samplerLookup = _toMap(samplers);
    }
    if (frame_buffers) {
      for (const [id, data] of Object.entries(frame_buffers)) {
        this.frameBuffers.set(id, data);
      }
    }
    // Re-setup interactions if they changed (e.g. new animation frames)
    if (interactions && this.interactions) {
      if (this.interactions.gui) {
        this.interactions.gui.destroy();
        this.interactions.gui = null;
      }
      try {
        await this.interactions.setup(this.scene.interactions);
      } catch (e) {
        console.warn('[engine] interactions setup in update() failed:', e.message || e);
      }
    }
    // Reconcile the compute DAG with the new pass set (a renderer with a
    // compute pass was toggled on/off, e.g. clip vectors). Without this, a
    // newly-added pass lives in scene.compute_passes but is never built into
    // the DAG and never runs. Done incrementally so unchanged passes keep their
    // converged buffers and in-flight readbacks.
    if (compute_passes) {
      const have = this.computeDAG ? this.computeDAG.passes : new Map();
      const want = this.scene.compute_passes.map(p => p.id);
      const changed = have.size !== want.length || want.some(id => !have.has(id));
      if (changed) {
        this._updating = true;
        try {
          await this._syncComputeDAG();
        } catch (e) {
          console.error('[engine] _syncComputeDAG failed in update():', e.message || e);
        }
        this._updating = false;
      }
    }

    // Rebuild render pipelines from scratch — cheap relative to a full reload.
    this._updating = true;
    this.renderPassObjects = [];
    try {
      await this._createRenderPipelines();
    } catch (e) {
      console.error('[engine] _createRenderPipelines failed in update():', e.message || e);
    }
    this._updating = false;
    // The host dispatches this async update() WITHOUT awaiting it and then
    // immediately calls notifyDirty()+render(); those ran while _updating was
    // still true (pipelines mid-rebuild) and were dropped by _renderNow's guard.
    // Now that pipelines are ready, trigger the post-update frame ourselves.
    // notifyDirty() first so compute passes re-run — their output feeds the
    // render pass within the same command encoder, so the frame isn't stale.
    // (Previously this was masked whenever another renderer's compute kept a
    // continuous render loop alive; with a compute-only renderer like clip
    // vectors as the sole content, nothing else re-triggered a frame.)
    this.notifyDirty();
    this.render();
  }

  /**
   * Live-mode hook: the host has resized the canvas. Recreate depth + MSAA
   * targets and re-render. The host is also responsible for updating the
   * camera uniform (aspect ratio change).
   */
  handleResize() {
    const dpr = window.devicePixelRatio || 1;
    const rect = this.canvas.getBoundingClientRect();
    const w = rect.width > 0 ? Math.round(rect.width * dpr) : this.canvas.width;
    const h = rect.height > 0 ? Math.round(rect.height * dpr) : this.canvas.height;
    if (w === this.width && h === this.height) return;
    if (w === 0 || h === 0) return;
    if (this.depthTexture) this.depthTexture.destroy();
    if (this.msaaTexture)  this.msaaTexture.destroy();
    this._createDepthAndMSAA();
    // Refresh the camera uniform (aspect/width/height) for the new size.
    this._updateCameraBuffer();
    this.render();
  }

  /** Live-mode hook: host sets the camera transform (reset/view/bookmark). */
  setCameraTransform(matrix, center) {
    if (!this.camera) return;
    this._suppressCameraNotify = true;
    try {
      if (matrix) this.camera.transform._mat = Float64Array.from(matrix);
      if (center) this.camera.transform._center = Array.from(center);
      this._updateCameraBuffer();
      if (this._cameraBufferId) this.computeDAG.markDirty(this._cameraBufferId);
      this.render();
    } finally {
      this._suppressCameraNotify = false;
    }
  }

  /**
   * Live-mode hook: recenter the camera on a world-space point (double-click).
   *
   * Applied to the engine's OWN (authoritative) camera using its current
   * matrix, so the view-translate and the rotation pivot (_center) update
   * together and can't desync. Doing this host-side instead — pushing a
   * Python-computed matrix+center via setCameraTransform — risks the pivot and
   * the matrix disagreeing, which makes the next rotate orbit the old center
   * and visibly snap the view back. The move is notified to the host so its
   * camera mirror (used for picking/bookmarks) stays in sync.
   */
  setCenter(point) {
    if (!this.camera || !point) return;
    this.camera.transform.setCenter(Array.from(point));
    this._updateCameraBuffer();
    if (this._cameraBufferId) this.computeDAG.markDirty(this._cameraBufferId);
    this.render();
    this._notifyCameraChanged(true);
  }

  /** Live-mode hook: host switches between orthographic and perspective. */
  setProjection(orthographic) {
    if (!this.camera) return;
    this.camera.orthographic = !!orthographic;
    this._updateCameraBuffer();
    if (this._cameraBufferId) this.computeDAG.markDirty(this._cameraBufferId);
    this.render();
  }

  /** Live-mode hook: host sets the background clear color [r,g,b,a] (0..1). */
  setClearColor(rgba) {
    this._clearColorOverride = rgba || null;
    this._applyTheme();
    this.render();
  }

  /**
   * Live-mode hook: the host acks that it finished processing the last input
   * event, releasing the input handler's move backpressure so the next
   * coalesced move can be sent.
   */
  ackInput() {
    if (this.input && this.input._releaseMove) this.input._releaseMove();
  }

  /** Report the current camera transform to the host (debounced). */
  _notifyCameraChanged(immediate = false) {
    if (!this._onCameraChanged || this._suppressCameraNotify) return;
    const send = () => {
      this._camNotifyTimer = null;
      try {
        this._onCameraChanged({
          matrix: Array.from(this.camera.transform._mat),
          center: Array.from(this.camera.transform._center),
        });
      } catch (e) {
        console.warn('[engine] on_camera_changed failed:', e && (e.message || e));
      }
    };
    if (immediate) {
      if (this._camNotifyTimer) { clearTimeout(this._camNotifyTimer); }
      send();
      return;
    }
    if (this._camNotifyTimer) return;  // trailing-edge debounce already scheduled
    this._camNotifyTimer = setTimeout(send, 100);
  }

  /**
   * Live-mode hook: mark a buffer (or a list of them) as dirty so any
   * compute pass triggered by it re-runs on the next render. Pass no args
   * to mark every compute trigger dirty (used after pipeline rebuilds).
   */
  notifyDirty(bufferIds) {
    if (!bufferIds) {
      for (const cp of this.scene.compute_passes) {
        for (const t of cp.triggers) this.computeDAG.markDirty(t);
      }
      return;
    }
    const ids = Array.isArray(bufferIds) ? bufferIds : [bufferIds];
    for (const id of ids) this.computeDAG.markDirty(id);
  }

  // =========================================================================
  // GPU resource creation
  // =========================================================================

  _createBuffers() {
    const device = this.device;
    // Create GPU buffers (skip CPU-only frame snapshots)
    for (const [id, info] of Object.entries(this.scene.buffers)) {
      if (info.usage === 'frame') {
        // CPU-only blob (e.g. animation frame snapshot). Keep raw bytes
        // available via this.frameBuffers for interactions to upload later.
        this.frameBuffers.set(id, info.data);
        continue;
      }
      const buf = device.createBuffer({
        size: info.size,
        usage: bufferUsageFlags(info.usage),
        label: id,
      });
      if (info.data && info.data.byteLength > 0) {
        device.queue.writeBuffer(buf, 0, info.data);
      }
      this.buffers.set(id, buf);
    }
  }

  _createTextures() {
    const device = this.device;
    // Create textures
    for (const [id, info] of Object.entries(this.scene.textures)) {
      const tex = device.createTexture({
        size: [info.width, info.height],
        format: info.format,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
        label: id,
      });
      if (info.data && info.data.byteLength > 0) {
        const bytesPerRow = _bytesPerPixel(info.format) * info.width;
        device.queue.writeTexture(
          { texture: tex },
          info.data,
          { bytesPerRow },
          [info.width, info.height],
        );
      }
      this.textures.set(id, tex);
      this.texViews.set(id, tex.createView());

      // Create sampler if descriptor present
      if (info.sampler) {
        // The sampler is stored on the texture info; we need to associate it
        // with the sampler_* id used in bindings. Find it by scanning render passes.
        const sampler = device.createSampler(info.sampler);
        // Store under tex id — we'll resolve sampler_ ids below
        this.samplers.set(id, sampler);
      }
    }

    // Build sampler_id → GPUSampler mapping by finding which texture each sampler references.
    // Convention: sampler and texture are co-located (sampler at binding N+1 of texture at N).
    this._samplerLookup = new Map(); // sampler_id → GPUSampler
    const allPasses = [...this.scene.render_passes, ...this.scene.compute_passes];
    for (const pass of allPasses) {
      for (const [bStr, resId] of Object.entries(pass.bindings)) {
        if (resId.startsWith('sampler_')) {
          // Find the texture at binding - 1
          const bNum = parseInt(bStr);
          const texResId = pass.bindings[String(bNum - 1)];
          if (texResId && this.samplers.has(texResId)) {
            this._samplerLookup.set(resId, this.samplers.get(texResId));
          } else {
            console.warn(`[engine] sampler lookup failed: ${resId}`);
          }
        }
      }
    }
  }

  _createDepthAndMSAA() {
    const dpr = window.devicePixelRatio || 1;
    const rect = this.canvas.getBoundingClientRect();
    const w = rect.width > 0 ? Math.round(rect.width * dpr) : this.canvas.width;
    const h = rect.height > 0 ? Math.round(rect.height * dpr) : this.canvas.height;
    // Set canvas backing resolution
    this.canvas.width = w;
    this.canvas.height = h;
    this.width = w;
    this.height = h;

    this.depthTexture = this.device.createTexture({
      size: [w, h],
      format: DEPTH_FORMAT,
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
      sampleCount: SAMPLE_COUNT,
      label: 'depth',
    });

    this.msaaTexture = this.device.createTexture({
      size: [w, h],
      format: this.canvasFormat,
      usage: GPUTextureUsage.RENDER_ATTACHMENT,
      sampleCount: SAMPLE_COUNT,
      label: 'msaa',
    });
  }

  // =========================================================================
  // Render pipeline creation
  // =========================================================================

  async _createRenderPipelines() {
    for (const rp of this.scene.render_passes) {
      const obj = await this._buildRenderPassObject(rp);
      this.renderPassObjects.push(obj);
    }
  }

  async _buildRenderPassObject(rp) {
    const device = this.device;
    const bindings = this._intKeyBindings(rp.bindings);

    const module = device.createShaderModule({ code: rp.shader, label: rp.id });

    const blend = blendFor(rp);

    // Build explicit layout — avoids layout:"auto" which strips unreachable bindings
    const vis = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
    const layoutEntries = buildLayoutEntries(bindings, vis, null);
    const bindGroupLayout = device.createBindGroupLayout({ entries: layoutEntries });
    const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] });

    // Build vertex buffer layouts from data
    const vertexBufferLayouts = (rp.vertex_buffers || []).map(vb => ({
      arrayStride: vb.arrayStride,
      stepMode: vb.stepMode || 'vertex',
      attributes: vb.attributes.map(a => ({
        format: a.format,
        offset: a.offset,
        shaderLocation: a.shaderLocation,
      })),
    }));

    const pipeline = await device.createRenderPipelineAsync({
      layout: pipelineLayout,
      vertex: { module, entryPoint: rp.vertex_entry_point || 'vertex_main', buffers: vertexBufferLayouts },
      fragment: {
        module,
        entryPoint: rp.fragment_entry_point || 'fragment_main',
        targets: [{ format: this.canvasFormat, blend }],
      },
      primitive: { topology: rp.topology || 'triangle-list' },
      depthStencil: {
        format: DEPTH_FORMAT,
        depthWriteEnabled: rp.depth_write,
        depthCompare: 'less',
        depthBias: rp.depth_bias || 0,
        depthBiasSlopeScale: rp.depth_bias_slope_scale || 0,
      },
      multisample: { count: SAMPLE_COUNT },
      label: rp.id,
    });

    // Build bind group entries
    const entries = this._buildBindGroupEntries(bindings);
    const bindGroup = device.createBindGroup({
      layout: bindGroupLayout,
      entries,
      label: rp.id,
    });

    // Indirect draw buffer reference
    let indirectBuffer = null;
    if (rp.draw_indirect) {
      indirectBuffer = this.buffers.get(rp.draw_indirect);
    }

    const vertexBufferRefs = (rp.vertex_buffers || []).map(vb => this.buffers.get(vb.buffer_id));

    let indexBuffer = null;
    let indexFormat = null;
    if (rp.index_buffer_id) {
      indexBuffer = this.buffers.get(rp.index_buffer_id);
      indexFormat = rp.index_format || 'uint32';
    }

    return {
      id: rp.id,
      enabled: true,
      pipeline,
      bindGroup,
      // Kept so the select pipeline can reuse the exact same explicit layout
      // (→ the same bind group is valid for both render and select pipelines)
      // and so _rebuildRenderBindGroups rebuilds against a stable layout.
      bindGroupLayout,
      vertexBufferLayouts,
      vertexCount: rp.vertex_count,
      instanceCount: rp.instance_count,
      drawIndirect: !!rp.draw_indirect,
      indirectBuffer,
      vertexBufferRefs,
      indexBuffer,
      indexFormat,
      // Select (object-id/pick) pass metadata; pipeline built lazily in
      // _ensureSelectPipelines(). null shader → this pass is not pickable.
      selectShader: rp.select_shader || null,
      selectEntryPoint: rp.select_entry_point || null,
      vertexEntryPoint: rp.vertex_entry_point || 'vertex_main',
      topology: rp.topology || 'triangle-list',
      depthBias: rp.depth_bias || 0,
      depthBiasSlopeScale: rp.depth_bias_slope_scale || 0,
      selectPipeline: null,
    };
  }

  _buildBindGroupEntries(bindings) {
    const entries = [];
    for (const [bindingNum, resId] of Object.entries(bindings)) {
      const b = parseInt(bindingNum);
      const type = resourceType(resId);

      if (type === 'buffer') {
        const buf = this.buffers.get(resId);
        if (!buf) console.error(`[engine]   MISSING buffer: ${resId} at binding ${b}`);
        entries.push({ binding: b, resource: { buffer: buf } });
      } else if (type === 'texture') {
        const view = this.texViews.get(resId);
        if (!view) console.error(`[engine]   MISSING texture view: ${resId} at binding ${b}`);
        entries.push({ binding: b, resource: view });
      } else if (type === 'sampler') {
        const sampler = this._samplerLookup.get(resId);
        if (!sampler) {
          console.error(`[engine]   MISSING sampler: ${resId} at binding ${b} (samplerLookup keys: ${[...this._samplerLookup.keys()]})`);
        } else {
          entries.push({ binding: b, resource: sampler });
        }
      } else if (type === 'storage-texture') {
        const view = this.texViews.get(resId);
        if (!view) console.error(`[engine]   MISSING storage texture view: ${resId} at binding ${b}`);
        entries.push({ binding: b, resource: view });
      }
    }
    return entries;
  }

  // =========================================================================
  // Camera
  // =========================================================================

  _updateCameraBuffer(w, h) {
    const buf = this.camera.updateUniforms(
      w || this.canvas.width || 1, h || this.canvas.height || 1
    );
    if (!buf) return;

    // Use buffer_id from scene metadata if available
    if (!this._cameraBufferId) {
      this._cameraBufferId = (this.scene.camera && this.scene.camera.buffer_id) || null;
      // Fallback: first uniform buffer
      if (!this._cameraBufferId) {
        for (const [id, info] of Object.entries(this.scene.buffers)) {
          if (info.usage === 'uniform') {
            this._cameraBufferId = id;
            break;
          }
        }
      }
    }
    if (this._cameraBufferId) {
      this.device.queue.writeBuffer(this.buffers.get(this._cameraBufferId), 0, buf);
    }
  }

  // =========================================================================
  // Light
  // =========================================================================

  _applyLight() {
    if (!this.scene.light || !this.scene.light.data) return;
    // Light data is base64-encoded bytes
    const b64 = this.scene.light.data;
    const raw = atob(b64);
    const data = new Uint8Array(raw.length);
    for (let i = 0; i < raw.length; i++) data[i] = raw.charCodeAt(i);

    // Use buffer_id from metadata, fallback to second uniform buffer
    let lightBufId = this.scene.light.buffer_id || null;
    if (!lightBufId) {
      for (const [id, info] of Object.entries(this.scene.buffers)) {
        if (info.usage === 'uniform' && id !== this._cameraBufferId) {
          lightBufId = id;
          break;
        }
      }
    }
    if (lightBufId) {
      this.device.queue.writeBuffer(this.buffers.get(lightBufId), 0, data);
    }
  }

  _rebuildRenderBindGroups() {
    for (let i = 0; i < this.renderPassObjects.length; i++) {
      const rp = this.scene.render_passes[i];
      const bindings = this._intKeyBindings(rp.bindings);
      const entries = this._buildBindGroupEntries(bindings);
      // Use the explicit layout the render pipeline was built with — the select
      // pipeline shares it, so the rebuilt bind group stays valid for both.
      const layout = this.renderPassObjects[i].bindGroupLayout
        || this.renderPassObjects[i].pipeline.getBindGroupLayout(0);
      this.renderPassObjects[i].bindGroup = this.device.createBindGroup({
        layout, entries, label: rp.id,
      });
      // Also update direct buffer references in case a buffer was recreated
      if (rp.vertex_buffers) {
        this.renderPassObjects[i].vertexBufferRefs = rp.vertex_buffers.map(vb => this.buffers.get(vb.buffer_id));
      }
      if (rp.draw_indirect) {
        this.renderPassObjects[i].indirectBuffer = this.buffers.get(rp.draw_indirect);
      }
      if (rp.index_buffer_id) {
        this.renderPassObjects[i].indexBuffer = this.buffers.get(rp.index_buffer_id);
      }
    }
  }

  // =========================================================================
  // Select (object-id / pick) pass
  // =========================================================================

  /**
   * Lazily build a "select" render pipeline for every pass that declared a
   * select shader. It reuses the pass's explicit bind-group layout (so the
   * pass's existing bind group is valid as-is) and renders the SELECT_PIPELINE
   * shader variant into an rgba32uint object-id target with a non-MSAA depth
   * buffer. Rebuilt automatically after update()/_createRenderPipelines, which
   * replace renderPassObjects (selectPipeline resets to null).
   */
  _ensureSelectPipelines() {
    if (!this.renderPassObjects) return;
    const device = this.device;
    for (const po of this.renderPassObjects) {
      if (po.selectPipeline === 'failed') continue;  // don't retry a bad pass every select
      if (po.selectPipeline || !po.selectShader || !po.selectEntryPoint) continue;
      try {
        const module = device.createShaderModule({ code: po.selectShader, label: po.id + '-select' });
        const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [po.bindGroupLayout] });
        po.selectPipeline = device.createRenderPipeline({
          layout: pipelineLayout,
          vertex: { module, entryPoint: po.vertexEntryPoint, buffers: po.vertexBufferLayouts },
          // Integer (uint) targets cannot blend.
          fragment: { module, entryPoint: po.selectEntryPoint, targets: [{ format: SELECT_FORMAT }] },
          primitive: { topology: po.topology },
          // Always write/test depth so the front-most object wins per pixel,
          // even for passes that are transparent in the visible render.
          depthStencil: {
            format: DEPTH_FORMAT,
            depthWriteEnabled: true,
            depthCompare: 'less',
            depthBias: po.depthBias || 0,
            depthBiasSlopeScale: po.depthBiasSlopeScale || 0,
          },
          multisample: { count: 1 },
          label: po.id + '-select',
        });
      } catch (e) {
        console.warn('[engine] select pipeline build failed for', po.id, e && (e.message || e));
        po.selectPipeline = 'failed';
      }
    }
  }

  /**
   * Live-mode hook: render the object-id / pick buffer into host-owned textures.
   *
   * The host (Python Scene._do_select / get_position) owns the select color +
   * depth textures and reads a pixel back itself; we just populate them. Unlike
   * the host's own Python select path, this reuses every pass's real GPU
   * buffers — including the ones the compute DAG filled (clip cross-section
   * cut_trigs + its indirect draw count, surface/clip vectors, …) — so the pick
   * buffer matches exactly what is drawn on screen. colorTexture must be
   * SELECT_FORMAT; depthTexture must be DEPTH_FORMAT; both non-MSAA and the same
   * size as the canvas.
   */
  renderSelectInto(colorTexture, depthTexture) {
    if (!this.device || !this.computeDAG) return;
    if (!colorTexture || !depthTexture) return;
    this._ensureSelectPipelines();

    const device = this.device;
    const encoder = device.createCommandEncoder();

    // Do NOT run the compute DAG here. The on-screen render loop owns the
    // execute()→processReadbacks() convergence cycle for count-then-fill passes
    // (clip cross-section, surface/clip vectors); running execute() out-of-band
    // from this hover-triggered path consumes the dirty set, bumps generation
    // counters and schedules readbacks at the wrong time, breaking that
    // convergence (vectors stop drawing, stale geometry appears mid-gesture).
    // The select pass only reads the already-filled buffers, so reuse them
    // as-is — the latest on-screen frame has populated them.

    const pass = encoder.beginRenderPass({
      colorAttachments: [{
        view: colorTexture.createView(),
        loadOp: 'clear',
        storeOp: 'store',
        clearValue: { r: 0, g: 0, b: 0, a: 0 },
      }],
      depthStencilAttachment: {
        view: depthTexture.createView(),
        depthLoadOp: 'clear',
        depthStoreOp: 'store',
        depthClearValue: 1.0,
      },
    });

    for (const po of this.renderPassObjects) {
      if (po.enabled === false || !po.selectPipeline || po.selectPipeline === 'failed') continue;
      pass.setPipeline(po.selectPipeline);
      pass.setBindGroup(0, po.bindGroup);
      if (po.vertexBufferRefs) {
        for (let i = 0; i < po.vertexBufferRefs.length; i++) {
          pass.setVertexBuffer(i, po.vertexBufferRefs[i]);
        }
      }
      if (po.indexBuffer) {
        pass.setIndexBuffer(po.indexBuffer, po.indexFormat);
      }
      if (po.drawIndirect && po.indexBuffer) {
        pass.drawIndexedIndirect(po.indirectBuffer, 0);
      } else if (po.drawIndirect) {
        pass.drawIndirect(po.indirectBuffer, 0);
      } else if (po.indexBuffer) {
        pass.drawIndexed(po.vertexCount, po.instanceCount);
      } else {
        pass.draw(po.vertexCount, po.instanceCount);
      }
    }

    pass.end();
    device.queue.submit([encoder.finish()]);

    // Return the exact camera transform the depth was rendered with, so the
    // host reconstructs pick positions with a matching model_view_projection.
    // (Browser-side zoom/pan reach the host's camera mirror only via the
    // debounced on_camera_changed, so without this the host's matrix can lag
    // the rendered depth and pick coordinates come out mis-scaled.)
    return {
      matrix: Array.from(this.camera.transform._mat),
      center: Array.from(this.camera.transform._center),
    };
  }

  // =========================================================================
  // Render
  // =========================================================================

  /**
   * Coalesced render entry point. Many render requests within a single frame
   * (e.g. dragging the clipping plane fires a mousemove + scene.render() per
   * event, each marking the clipping compute pass dirty) collapse into one
   * requestAnimationFrame, so the compute DAG + render pass run at most once
   * per frame instead of once per event.
   *
   * Callers that need the actual frame to run (capture, readback) still work:
   * a frame is guaranteed to be scheduled, and capture requests / readbacks are
   * processed when it runs.
   */
  render() {
    if (this._headless) { this._renderNow(); return; }
    if (this._renderScheduled) return;
    if (typeof requestAnimationFrame === 'undefined') { this._renderNow(); return; }
    this._renderScheduled = true;
    requestAnimationFrame(() => {
      this._renderScheduled = false;
      this._renderNow();
    });
  }

  _renderNow(captureTarget = null, autoReadback = true) {
    if (!this.device || !this.context) return;
    if (this.width === 0 || this.height === 0) return;
    if (this._updating) return;

    const device = this.device;
    const encoder = device.createCommandEncoder();

    // Run compute passes
    this.computeDAG.execute(device, encoder, this.buffers);

    const target = captureTarget || (this._headless ? this._ensureCaptureTexture() : null);
    const canvasTexture = target || this.context.getCurrentTexture();

    // Reconcile our MSAA/depth attachments to the ACTUAL swapchain size. The
    // canvas backing store (which getCurrentTexture() follows) is also resized by
    // the host (Python Canvas.resize) on layout changes, independently of our
    // handleResize(). Between those two events a frame would mix a fresh
    // swapchain with a stale-size MSAA target — WebGPU rejects the whole command
    // buffer ("resolve target size does not match the other attachments" + a
    // flood of "[Invalid CommandBuffer]"). Recreating on mismatch here makes the
    // render pass self-consistent every frame, whoever triggered the resize.
    if (!this.msaaTexture
        || this.msaaTexture.width !== canvasTexture.width
        || this.msaaTexture.height !== canvasTexture.height) {
      const w = canvasTexture.width, h = canvasTexture.height;
      if (this.depthTexture) this.depthTexture.destroy();
      if (this.msaaTexture) this.msaaTexture.destroy();
      this.depthTexture = device.createTexture({
        size: [w, h], format: DEPTH_FORMAT,
        usage: GPUTextureUsage.RENDER_ATTACHMENT, sampleCount: SAMPLE_COUNT,
        label: 'depth',
      });
      this.msaaTexture = device.createTexture({
        size: [w, h], format: this.canvasFormat,
        usage: GPUTextureUsage.RENDER_ATTACHMENT, sampleCount: SAMPLE_COUNT,
        label: 'msaa',
      });
      this.width = w; this.height = h;
      this._updateCameraBuffer();   // refresh aspect/width/height for the new size
    }

    const renderPass = encoder.beginRenderPass({
      colorAttachments: [{
        view: this.msaaTexture.createView(),
        resolveTarget: (target || canvasTexture).createView(),
        loadOp: 'clear',
        storeOp: 'store',
        clearValue: this.clearColor || LIGHT_CLEAR_COLOR,
      }],
      depthStencilAttachment: {
        view: this.depthTexture.createView(),
        depthLoadOp: 'clear',
        depthStoreOp: 'store',
        depthClearValue: 1.0,
      },
    });

    for (const pass of this.renderPassObjects) {
      if (pass.enabled === false) continue;
      renderPass.setPipeline(pass.pipeline);
      renderPass.setBindGroup(0, pass.bindGroup);
      if (pass.vertexBufferRefs) {
        for (let i = 0; i < pass.vertexBufferRefs.length; i++) {
          renderPass.setVertexBuffer(i, pass.vertexBufferRefs[i]);
        }
      }
      if (pass.indexBuffer) {
        renderPass.setIndexBuffer(pass.indexBuffer, pass.indexFormat);
      }
      if (pass.drawIndirect && pass.indexBuffer) {
        // Indexed geometry (e.g. arrow glyphs) with a GPU-computed instance
        // count: 5-u32 indirect buffer filled by the countThenFill pass.
        renderPass.drawIndexedIndirect(pass.indirectBuffer, 0);
      } else if (pass.drawIndirect) {
        renderPass.drawIndirect(pass.indirectBuffer, 0);
      } else if (pass.indexBuffer) {
        renderPass.drawIndexed(pass.vertexCount, pass.instanceCount);
      } else {
        renderPass.draw(pass.vertexCount, pass.instanceCount);
      }
    }

    renderPass.end();

    // If a captureNextFrame() is pending, copy the resolved canvas texture
    // into a staging buffer before submission.
    let pendingCaptures = null;
    let captureBuffer = null;
    let captureMeta = null;
    if (this._frameCaptureRequests && this._frameCaptureRequests.length) {
      pendingCaptures = this._frameCaptureRequests;
      this._frameCaptureRequests = [];
      const w = canvasTexture.width;
      const h = canvasTexture.height;
      const bpp = _bytesPerPixel(this.canvasFormat) || 4;
      const bytesPerRow = Math.ceil((w * bpp) / 256) * 256;
      captureBuffer = device.createBuffer({
        size: bytesPerRow * h,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
        label: 'frame-capture',
      });
      encoder.copyTextureToBuffer(
        { texture: canvasTexture },
        { buffer: captureBuffer, bytesPerRow, rowsPerImage: h },
        [w, h, 1],
      );
      captureMeta = { width: w, height: h, bytesPerRow, format: this.canvasFormat };
    }

    device.queue.submit([encoder.finish()]);

    if (pendingCaptures) {
      captureBuffer.mapAsync(GPUMapMode.READ).then(() => {
        const src = new Uint8Array(captureBuffer.getMappedRange()).slice();
        captureBuffer.unmap();
        captureBuffer.destroy();
        // Strip row padding so callers get tight width*bpp*height bytes.
        const { width, height, bytesPerRow, format } = captureMeta;
        const bpp = _bytesPerPixel(format) || 4;
        const tight = new Uint8Array(width * height * bpp);
        for (let y = 0; y < height; y++) {
          tight.set(src.subarray(y * bytesPerRow, y * bytesPerRow + width * bpp), y * width * bpp);
        }
        // Resolve with the raw ArrayBuffer so the websocket bridge transfers
        // the bytes via its dedicated buffer channel rather than serializing
        // a typed-array proxy.
        for (const resolve of pendingCaptures) resolve({ data: tight.buffer, width, height, format });
      });
    }

    // Async readback for count-then-fill passes. Skipped when a caller drives
    // the readbacks itself (settle()) to converge synchronously.
    if (autoReadback) {
      this.computeDAG.processReadbacks(this.device, this.buffers, () => {
        this._rebuildRenderBindGroups();
        this.render();
      });
    }
  }

  // =========================================================================
  // Resize
  // =========================================================================

  _onResize() {
    const dpr = window.devicePixelRatio || 1;
    const rect = this.canvas.getBoundingClientRect();
    const w = Math.round(rect.width * dpr);
    const h = Math.round(rect.height * dpr);
    if (w === this.width && h === this.height) return;
    if (w === 0 || h === 0) return;

    this.canvas.width = w;
    this.canvas.height = h;
    this.width = w;
    this.height = h;

    // Recreate depth and MSAA textures
    this.depthTexture.destroy();
    this.msaaTexture.destroy();
    this._createDepthAndMSAA();

    this._updateCameraBuffer();
    this.render();
  }

  // =========================================================================
  // Helpers
  // =========================================================================

  /** Convert string-keyed bindings dict to int-keyed object */
  _intKeyBindings(bindings) {
    const out = {};
    for (const [k, v] of Object.entries(bindings)) out[parseInt(k)] = v;
    return out;
  }

  _isDarkMode() {
    try {
      // Prefer explicit page-level theme (e.g. pydata-sphinx-theme toggle)
      const htmlTheme = document.documentElement && document.documentElement.dataset && document.documentElement.dataset.theme;
      if (htmlTheme === 'dark') return true;
      if (htmlTheme === 'light') return false;
      // Fall back to OS-level preference
      return !!(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
    } catch {
      return false;
    }
  }

  _applyTheme() {
    const dark = this._isDarkMode();
    const ov = this._clearColorOverride;
    this.clearColor = ov
      ? { r: ov[0], g: ov[1], b: ov[2], a: ov.length > 3 ? ov[3] : 1.0 }
      : (dark ? DARK_CLEAR_COLOR : LIGHT_CLEAR_COLOR);
    if (this.canvas && this.canvas.style) {
      this.canvas.style.backgroundColor = ov
        ? `rgb(${Math.round(ov[0] * 255)},${Math.round(ov[1] * 255)},${Math.round(ov[2] * 255)})`
        : (dark ? DARK_CANVAS_BG : LIGHT_CANVAS_BG);
    }
    // Update colorbar background color to match theme
    if (this.scene && this.scene.theme && this.scene.theme.buffer_id) {
      const buf = this.buffers && this.buffers.get(this.scene.theme.buffer_id);
      if (buf) {
        const c = this.clearColor;
        this.device.queue.writeBuffer(buf, 16, new Float32Array([c.r, c.g, c.b]));
      }
    }
    if (this.scene && this.scene.theme && Array.isArray(this.scene.theme.label_buffers)) {
      const c = this.clearColor;
      const lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
      const labelColor = Float32Array.from(lum < 0.5 ? DARK_LABEL_COLOR : LIGHT_LABEL_COLOR);
      for (const lb of this.scene.theme.label_buffers) {
        if (!lb || !lb.buffer_id) continue;
        const buf = this.buffers && this.buffers.get(lb.buffer_id);
        if (buf) this.device.queue.writeBuffer(buf, lb.offset | 0, labelColor);
      }
    }
  }

  _setupThemeObserver() {
    if (typeof window === 'undefined' || !window.matchMedia) return;
    this._themeMediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
    this._onThemeChange = () => {
      this._applyTheme();
      this.render();
    };
    if (this._themeMediaQuery.addEventListener) {
      this._themeMediaQuery.addEventListener('change', this._onThemeChange);
    } else if (this._themeMediaQuery.addListener) {
      this._themeMediaQuery.addListener(this._onThemeChange);
    }
    // Also observe data-theme attribute changes (e.g. pydata-sphinx-theme)
    if (document.documentElement && typeof MutationObserver !== 'undefined') {
      this._themeAttrObserver = new MutationObserver(() => {
        this._applyTheme();
        this.render();
      });
      this._themeAttrObserver.observe(document.documentElement, {
        attributes: true,
        attributeFilter: ['data-theme'],
      });
    }
  }

  _teardownThemeObserver() {
    if (!this._themeMediaQuery || !this._onThemeChange) return;
    if (this._themeMediaQuery.removeEventListener) {
      this._themeMediaQuery.removeEventListener('change', this._onThemeChange);
    } else if (this._themeMediaQuery.removeListener) {
      this._themeMediaQuery.removeListener(this._onThemeChange);
    }
    this._themeMediaQuery = null;
    this._onThemeChange = null;
    if (this._themeAttrObserver) {
      this._themeAttrObserver.disconnect();
      this._themeAttrObserver = null;
    }
  }

  dispose() {
    if (this._resizeObserver) this._resizeObserver.disconnect();
    if (this._camNotifyTimer) { clearTimeout(this._camNotifyTimer); this._camNotifyTimer = null; }
    this._teardownThemeObserver();
    if (this.input) this.input.dispose();
    if (this.interactions) this.interactions.dispose();
    if (this.depthTexture) this.depthTexture.destroy();
    if (this.msaaTexture) this.msaaTexture.destroy();
    // In live mode the host owns buffers/textures/samplers and the device.
    if (this.mode !== 'live') {
      for (const buf of this.buffers.values()) buf.destroy();
      for (const tex of this.textures.values()) tex.destroy();
      if (this.device) this.device.destroy();
    } else if (this.computeDAG) {
      // Live mode: the host owns the buffers/textures it supplied and frees
      // them itself (Python GC -> deferred flush_pending_destroys). But buffers
      // the ENGINE created — countThenFill compute outputs it resized, plus
      // indirect/staging/cap scratch — have no Python wrapper, so free them
      // here or they leak each time a live scene is discarded.
      this.computeDAG.dispose(this.buffers);
    }
  }
}

// ---------------------------------------------------------------------------
// Helpers shared by createLive
// ---------------------------------------------------------------------------

/** Accept either a Map or a plain object and return a Map. */
function _toMap(x) {
  if (!x) return new Map();
  if (x instanceof Map) return x;
  return new Map(Object.entries(x));
}

/** Recover buffer usage category from the id prefix used by BufferRegistry. */
function _usageFromId(id) {
  if (id.startsWith('uniform_'))  return 'uniform';
  if (id.startsWith('storage_'))  return 'storage';
  if (id.startsWith('vertex_'))   return 'vertex';
  if (id.startsWith('index_'))    return 'index';
  if (id.startsWith('indirect_')) return 'indirect';
  return 'storage';
}

// ---------------------------------------------------------------------------
// Texture format → bytes per pixel
// ---------------------------------------------------------------------------

function _bytesPerPixel(format) {
  const map = {
    'rgba8unorm': 4, 'rgba8snorm': 4, 'rgba8uint': 4, 'rgba8sint': 4,
    'bgra8unorm': 4, 'rg32float': 8, 'rgba16float': 8, 'rgba32float': 16,
    'r8unorm': 1, 'r16float': 2, 'r32float': 4, 'rg8unorm': 2, 'rg16float': 4,
  };
  return map[format] || 4;
}

// webgpuSetLowPower/webgpuSetHighPerformance let a user override the adapter
// choice from the browser's JS console. webgpuSelectDevice is the entry point
// the host runtime (Python) calls so it picks a device by the same rules as
// RenderEngine.init(); webgpuProbeDevice is exposed alongside it for debugging.
if (typeof globalThis !== 'undefined') {
  globalThis.webgpuSetLowPower = webgpuSetLowPower;
  globalThis.webgpuSetHighPerformance = webgpuSetHighPerformance;
  globalThis.webgpuSelectDevice = webgpuSelectDevice;
}

{ RenderEngine, webgpuSetLowPower, webgpuSetHighPerformance, webgpuSelectDevice };

if (typeof window !== 'undefined') { window.RenderEngine = RenderEngine; }
}
(function () {
  var create = RenderEngine.create;
  RenderEngine.create = function (id) {
    if (navigator.gpu) return create.apply(this, arguments);
    var canvas = document.getElementById(id), root = canvas.parentElement;
    var overlay = root.querySelector('div'), label = overlay.querySelector('span');
    canvas.style.display = 'none';
    root.querySelector('img').style.display = 'block';
    overlay.style.display = 'flex';
    overlay.style.background = 'rgba(0,0,0,0.45)';
    overlay.style.cursor = 'default';
    overlay.onmouseover = overlay.onmouseout = overlay.onclick = null;
    label.style.opacity = '1';
    label.style.fontSize = '1em';
    label.style.textAlign = 'center';
    label.style.padding = '0 1em';
    label.textContent = window.isSecureContext
      ? 'The 3D view needs WebGPU, which this browser does not have.'
      : 'The 3D view needs WebGPU, which browsers give only to https and localhost pages.';
    return Promise.resolve();
  };
})();
