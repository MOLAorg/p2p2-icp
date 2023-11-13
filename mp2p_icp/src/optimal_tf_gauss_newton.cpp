/* -------------------------------------------------------------------------
 *  A repertory of multi primitive-to-primitive (MP2P) ICP algorithms in C++
 * Copyright (C) 2018-2023 Jose Luis Blanco, University of Almeria
 * See LICENSE for license information.
 * ------------------------------------------------------------------------- */
/**
 * @file   optimal_tf_gauss_newton.cpp
 * @brief  Simple non-linear optimizer to find the SE(3) optimal transformation
 * @author Jose Luis Blanco Claraco
 * @date   Jun 16, 2019
 */

#include <mp2p_icp/errorTerms.h>
#include <mp2p_icp/optimal_tf_gauss_newton.h>
#include <mp2p_icp/robust_kernels.h>
#include <mrpt/poses/Lie/SE.h>

#include <Eigen/Dense>
#include <iostream>

using namespace mp2p_icp;

bool mp2p_icp::optimal_tf_gauss_newton(
    const Pairings& in, OptimalTF_Result& result,
    const OptimalTF_GN_Parameters& gnParams)
{
    using std::size_t;

    MRPT_START

    // Run Gauss-Newton steps, using SE(3) relinearization at the current
    // solution:
    ASSERTMSG_(
        gnParams.linearizationPoint.has_value(),
        "This method requires a linearization point");

    result.optimalPose = gnParams.linearizationPoint.value();

    const robust_sqrt_weight_func_t robustSqrtWeightFunc =
        mp2p_icp::create_robust_kernel(gnParams.kernel, gnParams.kernelParam);

    const auto nPt2Pt = in.paired_pt2pt.size();
    const auto nPt2Ln = in.paired_pt2ln.size();
    const auto nPt2Pl = in.paired_pt2pl.size();
    const auto nPl2Pl = in.paired_pl2pl.size();
    const auto nLn2Ln = in.paired_ln2ln.size();

    Eigen::Vector<double, 6>    g = Eigen::Vector<double, 6>::Zero();
    Eigen::Matrix<double, 6, 6> H = Eigen::Matrix<double, 6, 6>::Zero();

    auto w = gnParams.pairWeights;

    const bool  has_per_pt_weight       = !in.point_weights.empty();
    auto        cur_point_block_weights = in.point_weights.begin();
    std::size_t cur_point_block_start   = 0;

    for (size_t iter = 0; iter < gnParams.maxInnerLoopIterations; iter++)
    {
        // (12x6 Jacobian)
        const auto dDexpe_de =
            mrpt::poses::Lie::SE<3>::jacob_dDexpe_de(result.optimalPose);

        double errNormSqr = 0;

        // Point-to-point:
        for (size_t idx_pt = 0; idx_pt < nPt2Pt; idx_pt++)
        {
            // Error:
            const auto&                             p = in.paired_pt2pt[idx_pt];
            mrpt::math::CMatrixFixed<double, 3, 12> J1;
            mrpt::math::CVectorFixedDouble<3>       ret =
                mp2p_icp::error_point2point(p, result.optimalPose, J1);

            // Get point weight:
            if (has_per_pt_weight)
            {
                if (idx_pt >=
                    cur_point_block_start + cur_point_block_weights->first)
                {
                    ASSERT_(cur_point_block_weights != in.point_weights.end());
                    ++cur_point_block_weights;  // move to next block
                    cur_point_block_start = idx_pt;
                }
                w.pt2pt = cur_point_block_weights->second;
            }

            // Apply robust kernel?
            double weight = w.pt2pt;
            if (robustSqrtWeightFunc)
                weight *= robustSqrtWeightFunc(ret.asEigen().squaredNorm());

            // Error and Jacobian:
            const Eigen::Vector3d err_i = weight * ret.asEigen();
            errNormSqr += err_i.squaredNorm();

            const Eigen::Matrix<double, 3, 6> Ji =
                weight * J1.asEigen() * dDexpe_de.asEigen();
            g += Ji.transpose() * err_i;
            H += Ji.transpose() * Ji;
        }

        // Point-to-line
        for (size_t idx_pt = 0; idx_pt < nPt2Ln; idx_pt++)
        {
            // Error
            const auto&                             p = in.paired_pt2ln[idx_pt];
            mrpt::math::CMatrixFixed<double, 3, 12> J1;
            mrpt::math::CVectorFixedDouble<3>       ret =
                mp2p_icp::error_point2line(p, result.optimalPose, J1);

            // Apply robust kernel?
            double weight = w.pt2ln;
            if (robustSqrtWeightFunc)
                weight *= robustSqrtWeightFunc(ret.asEigen().squaredNorm());

            // Error and Jacobian:
            const Eigen::Vector3d err_i = weight * ret.asEigen();
            errNormSqr += err_i.squaredNorm();

            const Eigen::Matrix<double, 3, 6> Ji =
                weight * J1.asEigen() * dDexpe_de.asEigen();
            g += Ji.transpose() * err_i;
            H += Ji.transpose() * Ji;
        }

        // Line-to-Line
        // Minimum angle to approach zero
        for (size_t idx_ln = 0; idx_ln < nLn2Ln; idx_ln++)
        {
            const auto&                             p = in.paired_ln2ln[idx_ln];
            mrpt::math::CMatrixFixed<double, 4, 12> J1;
            mrpt::math::CVectorFixedDouble<4>       ret =
                mp2p_icp::error_line2line(p, result.optimalPose, J1);

            // Apply robust kernel?
            double weight = w.ln2ln;
            if (robustSqrtWeightFunc)
                weight *= robustSqrtWeightFunc(ret.asEigen().squaredNorm());

            // Error and Jacobian:
            const Eigen::Vector4d err_i = weight * ret.asEigen();
            errNormSqr += err_i.squaredNorm();

            const Eigen::Matrix<double, 4, 6> Ji =
                weight * J1.asEigen() * dDexpe_de.asEigen();
            g += Ji.transpose() * err_i;
            H += Ji.transpose() * Ji;
        }

        // Point-to-plane:
        for (size_t idx_pl = 0; idx_pl < nPt2Pl; idx_pl++)
        {
            // Error:
            const auto&                             p = in.paired_pt2pl[idx_pl];
            mrpt::math::CMatrixFixed<double, 3, 12> J1;
            mrpt::math::CVectorFixedDouble<3>       ret =
                mp2p_icp::error_point2plane(p, result.optimalPose, J1);

            // Apply robust kernel?
            double weight = w.pt2pl;
            if (robustSqrtWeightFunc)
                weight *= robustSqrtWeightFunc(ret.asEigen().squaredNorm());

            // Error and Jacobian:
            const Eigen::Vector3d err_i = weight * ret.asEigen();
            errNormSqr += err_i.squaredNorm();

            const Eigen::Matrix<double, 3, 6> Ji =
                weight * J1.asEigen() * dDexpe_de.asEigen();
            g += Ji.transpose() * err_i;
            H += Ji.transpose() * Ji;
        }

        // Plane-to-plane (only direction of normal vectors):
        for (size_t idx_pl = 0; idx_pl < nPl2Pl; idx_pl++)
        {
            // Error term:
            const auto&                             p = in.paired_pl2pl[idx_pl];
            mrpt::math::CMatrixFixed<double, 3, 12> J1;
            mrpt::math::CVectorFixedDouble<3>       ret =
                mp2p_icp::error_plane2plane(p, result.optimalPose, J1);

            // Apply robust kernel?
            double weight = w.pl2pl;
            if (robustSqrtWeightFunc)
                weight *= robustSqrtWeightFunc(ret.asEigen().squaredNorm());

            const Eigen::Vector3d err_i = weight * ret.asEigen();
            errNormSqr += err_i.squaredNorm();

            const Eigen::Matrix<double, 3, 6> Ji =
                weight * J1.asEigen() * dDexpe_de.asEigen();
            g += Ji.transpose() * err_i;
            H += Ji.transpose() * Ji;
        }

        // Target error?
        const double errNorm = std::sqrt(errNormSqr);

        if (errNorm <= gnParams.maxCost) break;

        // 3) Solve Gauss-Newton:
        // g = J.transpose() * err;
        // H = J.transpose() * J;
        const Eigen::Matrix<double, 6, 1> delta =
            -H.colPivHouseholderQr().solve(g);

        // 4) add SE(3) increment:
        const auto dE = mrpt::poses::Lie::SE<3>::exp(
            mrpt::math::CVectorFixed<double, 6>(delta));

        result.optimalPose = result.optimalPose + dE;

        if (gnParams.verbose)
        {
            std::cout << "[P2P GN] iter:" << iter << " err:" << errNorm
                      << " delta:" << delta.transpose() << "\n";
        }

        // Simple convergence test:
        if (delta.norm() < gnParams.minDelta) break;

    }  // for each iteration

    return true;

    MRPT_END
}
