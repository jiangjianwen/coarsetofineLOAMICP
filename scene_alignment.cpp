//
// Created by jjwen on 2019/11/22.
//

#include <iostream>
#include <stdio.h>
#include <vector>
#include <iostream>
#include <pcl/registration/icp.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <ceres/ceres.h>
#include <ceres/rotation.h>
#include <eigen3/Eigen/Dense>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/kdtree/kdtree_flann.h>

using namespace std;

template <typename PT_DATA_TYPE>
class Scene_alignment
{
public:
    struct LidarEdgeFactor
    {
        LidarEdgeFactor(Eigen::Vector3d curr_point_, Eigen::Vector3d last_point_a_,
                        Eigen::Vector3d last_point_b_, double s_)
            : curr_point(curr_point_), last_point_a(last_point_a_), last_point_b(last_point_b_), s(s_) {}

        template <typename T>
        bool operator()(const T *q, const T *t, T *residual) const
        {

            Eigen::Matrix<T, 3, 1> cp{T(curr_point.x()), T(curr_point.y()), T(curr_point.z())};
            Eigen::Matrix<T, 3, 1> lpa{T(last_point_a.x()), T(last_point_a.y()), T(last_point_a.z())};
            Eigen::Matrix<T, 3, 1> lpb{T(last_point_b.x()), T(last_point_b.y()), T(last_point_b.z())};

            //Eigen::Quaternion<T> q_last_curr{q[3], T(s) * q[0], T(s) * q[1], T(s) * q[2]};
            Eigen::Quaternion<T> q_last_curr{q[3], q[0], q[1], q[2]};
            Eigen::Quaternion<T> q_identity{T(1), T(0), T(0), T(0)};
            q_last_curr = q_identity.slerp(T(s), q_last_curr);
            Eigen::Matrix<T, 3, 1> t_last_curr{T(s) * t[0], T(s) * t[1], T(s) * t[2]};

            Eigen::Matrix<T, 3, 1> lp;
            lp = q_last_curr * cp + t_last_curr;

            Eigen::Matrix<T, 3, 1> nu = (lp - lpa).cross(lp - lpb);
            Eigen::Matrix<T, 3, 1> de = lpa - lpb;

            residual[0] = nu.x() / de.norm();
            residual[1] = nu.y() / de.norm();
            residual[2] = nu.z() / de.norm();

            return true;
        }

        static ceres::CostFunction *Create(const Eigen::Vector3d curr_point_, const Eigen::Vector3d last_point_a_,
                                           const Eigen::Vector3d last_point_b_, const double s_)
        {
            return (new ceres::AutoDiffCostFunction<
                    LidarEdgeFactor, 3, 4, 3>(
                new LidarEdgeFactor(curr_point_, last_point_a_, last_point_b_, s_)));
        }

        Eigen::Vector3d curr_point, last_point_a, last_point_b;
        double s;
    };

    struct LidarPlaneNormFactor
    {

        LidarPlaneNormFactor(Eigen::Vector3d curr_point_, Eigen::Vector3d plane_unit_norm_,
                             double negative_OA_dot_norm_) : curr_point(curr_point_), plane_unit_norm(plane_unit_norm_),
                                                             negative_OA_dot_norm(negative_OA_dot_norm_) {}

        template <typename T>
        bool operator()(const T *q, const T *t, T *residual) const
        {
            Eigen::Quaternion<T> q_w_curr{q[3], q[0], q[1], q[2]};
            Eigen::Matrix<T, 3, 1> t_w_curr{t[0], t[1], t[2]};
            Eigen::Matrix<T, 3, 1> cp{T(curr_point.x()), T(curr_point.y()), T(curr_point.z())};
            Eigen::Matrix<T, 3, 1> point_w;
            point_w = q_w_curr * cp + t_w_curr;

            Eigen::Matrix<T, 3, 1> norm(T(plane_unit_norm.x()), T(plane_unit_norm.y()), T(plane_unit_norm.z()));
            residual[0] = norm.dot(point_w) + T(negative_OA_dot_norm);
            return true;
        }

        static ceres::CostFunction *Create(const Eigen::Vector3d curr_point_, const Eigen::Vector3d plane_unit_norm_,
                                           const double negative_OA_dot_norm_)
        {
            return (new ceres::AutoDiffCostFunction<
                    LidarPlaneNormFactor, 1, 4, 3>(
                new LidarPlaneNormFactor(curr_point_, plane_unit_norm_, negative_OA_dot_norm_)));
        }

        Eigen::Vector3d curr_point;
        Eigen::Vector3d plane_unit_norm;
        double negative_OA_dot_norm;
    };

    float m_line_res = 0.4;
    float m_plane_res = 0.8;

    double m_inliner_dis = 0.02;
    double m_inlier_ratio = 0.80;

    pcl::VoxelGrid<pcl::PointXYZI> m_down_sample_filter_line_source, m_down_sample_filter_line_target;
    pcl::VoxelGrid<pcl::PointXYZI> m_down_sample_filter_surface_source, m_down_sample_filter_surface_target;


    pcl::KdTreeFLANN<pcl::PointXYZI> m_kdtree_corner_from_map;
    pcl::KdTreeFLANN<pcl::PointXYZI> m_kdtree_surf_from_map;


    Eigen::Quaterniond m_q_w_curr;
    Eigen::Vector3d m_t_w_curr;

    Scene_alignment()
    {
    }

    ~Scene_alignment(){};

    double compute_inlier_residual_threshold(std::vector<double> residuals, double ratio)
    {
        std::set<double> dis_vec;
        for (size_t i = 0; i < (size_t)(residuals.size() / 3); i++)
        {
            dis_vec.insert(fabs(residuals[3 * i + 0]) + fabs(residuals[3 * i + 1]) + fabs(residuals[3 * i + 2]));
        }
        return *(std::next(dis_vec.begin(), (int)((ratio)*dis_vec.size())));
    }


    void find_tranfrom_of_two_mappings(pcl::PointCloud<pcl::PointXYZI> &keyframe_a_s, pcl::PointCloud<pcl::PointXYZI> &keyframe_a_l,
                                       pcl::PointCloud<pcl::PointXYZI> &keyframe_b_s, pcl::PointCloud<pcl::PointXYZI> &keyframe_b_l,
                                       double &m_inlier_threshold, Eigen::Quaterniond &quaternion, Eigen::Vector3d &trans,
                                       double &init_cost, double &final_cost)
    {

        std::cout << trans(0) << " " << trans(1) << " " << trans(2) << std::endl;

        double parameters[7] = {quaternion.x(), quaternion.y(), quaternion.z(), quaternion.w(), trans(0), trans(1), trans(2)};

        //                double parameters[7] = {quaternion.w(), quaternion.x(), quaternion.y(), quaternion.z(),  trans(0), trans(1), trans(2)};

        Eigen::Map<Eigen::Quaterniond> q_w_curr(parameters);
        Eigen::Map<Eigen::Vector3d> t_w_curr(parameters + 4);

        Eigen::Quaterniond q_wmap_wodom;
        Eigen::Vector3d t_wmap_wodom;

        q_wmap_wodom.x() = quaternion.x();
        q_wmap_wodom.y() = quaternion.y();
        q_wmap_wodom.z() = quaternion.z();
        q_wmap_wodom.w() = quaternion.w();
        t_wmap_wodom.x() = trans.x();
        t_wmap_wodom.y() = trans.y();
        t_wmap_wodom.z() = trans.z();

        Eigen::Quaterniond q_wodom_curr(1, 0, 0, 0);
        Eigen::Vector3d t_wodom_curr(0, 0, 0);

        pcl::PointCloud<pcl::PointXYZI> sourece_pt_plane = keyframe_a_s;
        pcl::PointCloud<pcl::PointXYZI> target_pt_plane = keyframe_b_s;
        pcl::PointCloud<pcl::PointXYZI> sourece_pt_line = keyframe_a_l;
        pcl::PointCloud<pcl::PointXYZI> target_pt_line = keyframe_b_l;

        pcl::PointCloud<pcl::PointXYZI> sourece_pt_plane_ds; // Point cloud of downsampled
        pcl::PointCloud<pcl::PointXYZI> target_pt_plane_ds;
        pcl::PointCloud<pcl::PointXYZI> sourece_pt_line_ds; // Point cloud of downsampled
        pcl::PointCloud<pcl::PointXYZI> target_pt_line_ds;

        m_down_sample_filter_surface_source.setInputCloud(sourece_pt_plane.makeShared());
        m_down_sample_filter_surface_target.setInputCloud(target_pt_plane.makeShared());
        m_down_sample_filter_line_source.setInputCloud(sourece_pt_line.makeShared());
        m_down_sample_filter_line_target.setInputCloud(target_pt_line.makeShared());

        //原始点云坐标
        pcl::PointCloud<pcl::PointXYZI> laserCloudOri;
        pcl::PointCloud<pcl::PointXYZI> coeffSel;

        for (int scale = 8; scale >= 0; scale -= 4)
        {

            int max_iteration = 2;

            float line_res = m_line_res * scale;
            float plane_res = m_plane_res * scale;

            if (line_res < m_line_res)
            {
                line_res = m_line_res;
            }

            if (plane_res < m_plane_res)
            {
                plane_res = m_plane_res;
                max_iteration = max_iteration * 2;
            }

            m_down_sample_filter_line_source.setLeafSize(line_res, line_res, line_res);
            m_down_sample_filter_surface_source.setLeafSize(plane_res, plane_res, plane_res);
            m_down_sample_filter_line_target.setLeafSize(line_res, line_res, line_res);
            m_down_sample_filter_surface_target.setLeafSize(plane_res, plane_res, plane_res);

            m_down_sample_filter_line_source.filter(sourece_pt_line_ds);
            m_down_sample_filter_surface_source.filter(sourece_pt_plane_ds);
            m_down_sample_filter_line_target.filter(target_pt_line_ds);
            m_down_sample_filter_surface_target.filter(target_pt_plane_ds);

            //                    std::cout << "Source pt plane size = " << sourece_pt_plane_ds.size() << std::endl;
            //                    std::cout << "Target pt plane size = " << target_pt_plane_ds.size() << std::endl;

            m_kdtree_corner_from_map.setInputCloud(target_pt_line_ds.makeShared());
            m_kdtree_surf_from_map.setInputCloud(target_pt_plane_ds.makeShared());

            int laserCloudCornerStackNum = sourece_pt_line_ds.points.size();
            int laserCloudSurfStackNum = sourece_pt_plane_ds.points.size();

            std::cout << "laserCloudCornerStackNum " << laserCloudCornerStackNum << std::endl;
            std::cout << "laserCloudSurfStackNum " << laserCloudSurfStackNum << std::endl;

            std::vector<int> pointSearchInd;
            std::vector<float> pointSearchSqDis;

            pcl::PointXYZI pointOri, pointSel;

            q_wodom_curr.x() = 0.0;
            q_wodom_curr.y() = 0.0;
            q_wodom_curr.z() = 0.0;
            q_wodom_curr.w() = 1.0;
            t_wodom_curr.x() = 0.0;
            t_wodom_curr.y() = 0.0;
            t_wodom_curr.z() = 0.0;

            q_w_curr = q_wmap_wodom * q_wodom_curr;
            t_w_curr = q_wmap_wodom * t_wodom_curr + t_wmap_wodom;

            for (int iterCount = 0; iterCount < max_iteration; iterCount++)
            {
                laserCloudOri.points.clear();
                coeffSel.points.clear();

                //ceres::LossFunction *loss_function = NULL;
                ceres::LossFunction *loss_function = new ceres::HuberLoss(0.1);
                ceres::LocalParameterization *q_parameterization = new ceres::EigenQuaternionParameterization();
                ceres::Problem::Options problem_options;

                ceres::ResidualBlockId block_id;
                std::vector<ceres::ResidualBlockId> residual_block_ids;

                ceres::Problem problem(problem_options);
                problem.AddParameterBlock(parameters, 4, q_parameterization);
                problem.AddParameterBlock(parameters + 4, 3);

                int testcornerNum = 0;

                for (int i = 0; i < laserCloudCornerStackNum; i++)
                {
                    pointOri = sourece_pt_line_ds.points[i];

                    Eigen::Vector3d point_curr(pointOri.x, pointOri.y, pointOri.z);
                    Eigen::Vector3d point_w = q_w_curr * point_curr + t_w_curr;
                    pointSel.x = point_w.x();
                    pointSel.y = point_w.y();
                    pointSel.z = point_w.z();
                    pointSel.intensity = pointOri.intensity;

                    m_kdtree_corner_from_map.nearestKSearch(pointSel, 5, pointSearchInd, pointSearchSqDis);

                    float min_corner_dis;
                    if (scale == 0)
                        min_corner_dis = 1.0;
                    else
                        min_corner_dis = 1.0 * scale;

                    if (pointSearchSqDis[4] < min_corner_dis)
                    {
                        testcornerNum++;

                        std::vector<Eigen::Vector3d> nearCorners;
                        Eigen::Vector3d center(0, 0, 0);
                        for (int j = 0; j < 5; j++)
                        {
                            Eigen::Vector3d tmp(target_pt_line_ds.points[pointSearchInd[j]].x,
                                                target_pt_line_ds.points[pointSearchInd[j]].y,
                                                target_pt_line_ds.points[pointSearchInd[j]].z);
                            center = center + tmp;
                            nearCorners.push_back(tmp);
                        }
                        center = center / 5.0;

                        Eigen::Matrix3d covMat = Eigen::Matrix3d::Zero();
                        for (int j = 0; j < 5; j++)
                        {
                            Eigen::Matrix<double, 3, 1> tmpZeroMean = nearCorners[j] - center;
                            covMat = covMat + tmpZeroMean * tmpZeroMean.transpose();
                        }

                        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> saes(covMat);

                        // if is indeed line feature
                        // note Eigen library sort eigenvalues in increasing order
                        Eigen::Vector3d unit_direction = saes.eigenvectors().col(2);
                        Eigen::Vector3d curr_point(pointOri.x, pointOri.y, pointOri.z);
                        if (saes.eigenvalues()[2] > 3 * saes.eigenvalues()[1])
                        {
                            Eigen::Vector3d point_on_line = center;
                            Eigen::Vector3d point_a, point_b;
                            point_a = 0.1 * unit_direction + point_on_line;
                            point_b = -0.1 * unit_direction + point_on_line;

                            ceres::CostFunction *cost_function = LidarEdgeFactor::Create(curr_point, point_a, point_b, 1.0);
                            block_id = problem.AddResidualBlock(cost_function, loss_function, parameters, parameters + 4);
                            residual_block_ids.push_back(block_id);
                        }
                    }
                }

                //                        std::cout<<"scale "<<scale<<" testcornerNum "<<testcornerNum<<std::endl;

                for (int i = 0; i < laserCloudSurfStackNum; i++)
                {
                    pointOri = sourece_pt_plane_ds.points[i];

                    Eigen::Vector3d point_curr(pointOri.x, pointOri.y, pointOri.z);
                    Eigen::Vector3d point_w = q_w_curr * point_curr + t_w_curr;
                    pointSel.x = point_w.x();
                    pointSel.y = point_w.y();
                    pointSel.z = point_w.z();
                    pointSel.intensity = pointOri.intensity;

                    m_kdtree_surf_from_map.nearestKSearch(pointSel, 5, pointSearchInd, pointSearchSqDis);

                    Eigen::Matrix<double, 5, 3> matA0;
                    Eigen::Matrix<double, 5, 1> matB0 = -1 * Eigen::Matrix<double, 5, 1>::Ones();

                    float min_surf_dis;
                    if (scale == 0)
                        min_surf_dis = 1.0;
                    else
                        min_surf_dis = 1.0 * scale;

                    if (pointSearchSqDis[4] < min_surf_dis)
                    {
                        for (int j = 0; j < 5; j++)
                        {
                            matA0(j, 0) = target_pt_plane_ds.points[pointSearchInd[j]].x;
                            matA0(j, 1) = target_pt_plane_ds.points[pointSearchInd[j]].y;
                            matA0(j, 2) = target_pt_plane_ds.points[pointSearchInd[j]].z;
                        }
                        // find the norm of plane
                        Eigen::Vector3d norm = matA0.colPivHouseholderQr().solve(matB0);
                        double negative_OA_dot_norm = 1 / norm.norm();
                        norm.normalize();

                        // Here n(pa, pb, pc) is unit norm of plane
                        bool planeValid = true;
                        for (int j = 0; j < 5; j++)
                        {
                            // if OX * n > 0.2, then plane is not fit well
                            if (fabs(norm(0) * target_pt_plane_ds.points[pointSearchInd[j]].x +
                                     norm(1) * target_pt_plane_ds.points[pointSearchInd[j]].y +
                                     norm(2) * target_pt_plane_ds.points[pointSearchInd[j]].z + negative_OA_dot_norm) > 0.2)
                            {
                                planeValid = false;
                                break;
                            }
                        }
                        Eigen::Vector3d curr_point(pointOri.x, pointOri.y, pointOri.z);
                        if (planeValid)
                        {
                            laserCloudOri.points.push_back(pointOri);

                            pcl::PointXYZI coeffn;

                            float pd2 = norm(0) * pointSel.x + norm(1) * pointSel.y + norm(2) * pointSel.z + negative_OA_dot_norm;

                            float s = 1 - 0.9 * fabs(pd2) / sqrt(sqrt(pointSel.x * pointSel.x + pointSel.y * pointSel.y + pointSel.z * pointSel.z));

                            coeffn.x = s * norm(0);
                            coeffn.y = s * norm(1);
                            coeffn.z = s * norm(2);
                            coeffSel.points.push_back(coeffn);

                            ceres::CostFunction *cost_function = LidarPlaneNormFactor::Create(curr_point, norm, negative_OA_dot_norm);
                            block_id = problem.AddResidualBlock(cost_function, loss_function, parameters, parameters + 4);
                            residual_block_ids.push_back(block_id);
                        }
                    }
                }

                ceres::Solver::Options options;
                options.linear_solver_type = ceres::DENSE_QR;
                options.max_num_iterations = 10;
                options.minimizer_progress_to_stdout = false;
                options.check_gradients = false;
                options.gradient_check_relative_precision = 1e-4;
                ceres::Solver::Summary summary;
                ceres::Solve(options, &problem, &summary);

                //                        printf("result q %f %f %f %f result t %f %f %f\n", parameters[3], parameters[0], parameters[1], parameters[2],
                //                                                                           parameters[4], parameters[5], parameters[6]);

                ceres::Problem::EvaluateOptions eval_options;
                eval_options.residual_blocks = residual_block_ids;
                double total_cost = 0.0;
                std::vector<double> residuals;
                problem.Evaluate(eval_options, &total_cost, &residuals, nullptr, nullptr);
                //double avr_cost = total_cost / residual_block_ids.size();

                double m_inliner_ratio_threshold = compute_inlier_residual_threshold(residuals, m_inlier_ratio);
                m_inlier_threshold = std::max(m_inliner_dis, m_inliner_ratio_threshold);

                m_inlier_threshold = m_inlier_threshold * summary.final_cost / summary.initial_cost;

                init_cost = summary.initial_cost;

                final_cost = summary.final_cost;

                //                        std::cout<<"m_inlier_threshold "<<m_inlier_threshold<<std::endl;

                std::cout << summary.BriefReport() << '\n';
            }

            q_wmap_wodom = q_w_curr * q_wodom_curr.inverse();
            t_wmap_wodom = t_w_curr - q_wmap_wodom * t_wodom_curr;

            //                    std::cout << "===*** Result of pc_reg: ===*** " << endl;
        }

        m_q_w_curr = q_w_curr;
        m_t_w_curr = t_w_curr;
    };


};