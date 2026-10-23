// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include "constraint_sketch/analysis.hpp"
#include "constraint_sketch/solver.hpp"

// constraints given by Python: (indices, rows, f) with f(values at indices) -> (residuals, Jacobian rows), for tests
struct PyConstraintSystem {
    std::vector<std::vector<size_t>> indices;
    std::vector<size_t> rows;
    std::vector<py::object> functions;

    explicit PyConstraintSystem(const py::list &constraints) {
        for (const py::handle &c : constraints) {
            const py::tuple t = c.cast<py::tuple>();
            indices.push_back(t[0].cast<std::vector<size_t>>());
            rows.push_back(t[1].cast<size_t>());
            functions.push_back(t[2].cast<py::object>());
        }
    }
    size_t Count() const { return functions.size(); }
    size_t Rows(size_t c) const { return rows[c]; }
    const std::vector<size_t> &Indices(size_t c) const { return indices[c]; }
    void Evaluate(size_t c, const std::vector<double> &x, double *r, double *J) const {
        std::vector<double> local;
        for (size_t i : indices[c])
            local.push_back(x[i]);
        const py::tuple out = functions[c](local).cast<py::tuple>();
        const auto res = out[0].cast<std::vector<double>>();
        const auto jac = out[1].cast<std::vector<std::vector<double>>>();
        if (res.size() != rows[c] || jac.size() != rows[c])
            throw std::invalid_argument("constraint " + std::to_string(c) + ": wrong number of rows");
        for (size_t i = 0; i < rows[c]; ++i) {
            if (jac[i].size() != indices[c].size())
                throw std::invalid_argument("constraint " + std::to_string(c) + ": wrong number of derivatives");
            r[i] = res[i];
            for (size_t k = 0; k < indices[c].size(); ++k)
                J[i * indices[c].size() + k] = jac[i][k];
        }
    }
};

static Unknowns UnknownsOf(const std::vector<double> &values, const std::vector<bool> &fixed, const std::vector<double> &scale) {
    Unknowns x(values);
    if (!fixed.empty() && fixed.size() != values.size())
        throw std::invalid_argument("fixed needs one entry per value");
    if (!scale.empty() && scale.size() != values.size())
        throw std::invalid_argument("scale needs one entry per value");
    for (size_t i = 0; i < fixed.size(); ++i)
        x.fixed[i] = fixed[i];
    if (!scale.empty())
        x.scale = scale;
    return x;
}

void BindSolver(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    m.def("_SolveConstraints", [](const std::vector<double> &values, const py::list &constraints, double size,
                                  const std::vector<bool> &fixed, const std::vector<double> &scale) {
        const PyConstraintSystem system(constraints);
        Unknowns x = UnknownsOf(values, fixed, scale);
        const SolveResult r = SolveConstraints(system, x, size);
        py::dict out;
        out["solved"] = r.solved;
        out["iterations"] = r.iterations;
        out["residual"] = r.residual;
        out["values"] = x.value;
        return out;
    }, py::arg("values"), py::arg("constraints"), py::arg("size") = 1.0, py::arg("fixed") = std::vector<bool>(),
          py::arg("scale") = std::vector<double>(), "test hook: solves constraints given as (indices, rows, f)");
    m.def("_AnalyzeConstraints", [](const std::vector<double> &values, const py::list &constraints,
                                    const std::vector<bool> &fixed, const std::vector<double> &scale) {
        const PyConstraintSystem system(constraints);
        const ConstraintAnalysis a = AnalyzeConstraints(system, UnknownsOf(values, fixed, scale));
        py::dict out;
        out["rank"] = a.rank;
        out["dof"] = a.dof;
        out["determined"] = std::vector<bool>(a.determined.begin(), a.determined.end());
        out["redundant"] = std::vector<bool>(a.redundant.begin(), a.redundant.end());
        out["amplification"] = a.amplification;
        return out;
    }, py::arg("values"), py::arg("constraints"), py::arg("fixed") = std::vector<bool>(),
          py::arg("scale") = std::vector<double>(), "test hook: rank, degrees of freedom, determined unknowns, redundant constraints, amplification");
    m.def("_SparseCholeskySolve", [](const std::vector<std::vector<double>> &a, const std::vector<double> &b,
                                     const std::vector<double> &shifts, const std::vector<std::vector<double>> &columns) {
        const size_t k = b.size();
        if (a.size() != k || std::any_of(a.begin(), a.end(), [&](const std::vector<double> &v) { return v.size() != k; }))
            throw std::invalid_argument("a must be square with one row per entry of b");
        for (const std::vector<double> &v : columns)
            if (v.size() != k)
                throw std::invalid_argument("every column needs one entry per entry of b");
        std::vector<Eigen::Triplet<double>> entries;
        for (size_t i = 0; i < k; ++i)
            for (size_t j = 0; j < k; ++j)
                if (a[i][j] != 0.0)
                    entries.emplace_back(i, j, a[i][j]);
        const Eigen::Index n = Eigen::Index(k);
        SparseMatrix A(n, n);
        A.setFromTriplets(entries.begin(), entries.end());
        Eigen::MatrixXd V(n, Eigen::Index(columns.size()));
        for (size_t c = 0; c < columns.size(); ++c)
            for (size_t i = 0; i < k; ++i)
                V(Eigen::Index(i), Eigen::Index(c)) = columns[c][i];
        SparseCholesky cholesky;
        cholesky.Analyze(A);
        py::list out;
        for (double shift : shifts) {
            if (!cholesky.Factor(A, shift, V)) {
                out.append(py::none());
                continue;
            }
            Eigen::VectorXd x(n);
            for (size_t i = 0; i < k; ++i)
                x[Eigen::Index(i)] = b[i];
            cholesky.Solve(x);
            out.append(std::vector<double>(x.begin(), x.end()));
        }
        return out;
    }, py::arg("a"), py::arg("b"), py::pos_only(), py::arg("shifts"), py::arg("columns") = std::vector<std::vector<double>>(),
          "test hook: per shift the x of (a + shift I + V V^T) x = b, V the columns, one analysis of a's pattern; None "
          "where it is not positive definite");
    m.def("_JacobiSVD", [](const std::vector<std::vector<double>> &rows) {
        const Eigen::Index height = Eigen::Index(rows.size()), width = rows.empty() ? 0 : Eigen::Index(rows[0].size());
        Eigen::MatrixXd A(height, width);
        for (Eigen::Index i = 0; i < height; ++i)
            for (Eigen::Index j = 0; j < width; ++j)
                A(i, j) = rows.at(size_t(i)).at(size_t(j));
        const Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV);
        const Eigen::Index k = std::min(height, width);
        Eigen::MatrixXd U = Eigen::MatrixXd::Zero(height, width);
        U.leftCols(k) = svd.matrixU().leftCols(k);
        std::vector<double> s(size_t(width), 0.0);
        for (Eigen::Index i = 0; i < k; ++i)
            s[size_t(i)] = svd.singularValues()[i];
        auto lists = [](const Eigen::MatrixXd &M) {
            std::vector<std::vector<double>> out(size_t(M.rows()), std::vector<double>(size_t(M.cols())));
            for (Eigen::Index i = 0; i < M.rows(); ++i)
                for (Eigen::Index j = 0; j < M.cols(); ++j)
                    out[size_t(i)][size_t(j)] = M(i, j);
            return out;
        };
        return py::make_tuple(lists(U), s, lists(svd.matrixV()));
    }, py::arg("rows"), "test hook: (U, s, V) with A = U diag(s) V^T, as many singular values as columns");
}
