//
// Created by qiayuan on 2021/11/16.
//
#pragma once

#include <cheetah_basic_controllers/cpp_types.h>

#include <Eigen/Dense>
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace cheetah_ros
{
using Eigen::Dynamic;
using Eigen::Matrix;
using Eigen::Matrix3d;
using Eigen::MatrixXd;
using Eigen::Quaterniond;
using Eigen::Vector3d;
using Eigen::VectorXd;

class MpcFormulation  // TODO: template for float
{
public:
  static constexpr int STATE_DIM = 13;   // 6 dof pose + 6 dof velocity + 1 gravity.
  static constexpr int ACTION_DIM = 12;  // 4 ground reaction force.

  void setup(int horizon, const Matrix<double, STATE_DIM, 1>& weight, double alpha);

  void buildStateSpace(double mass, const Matrix3d& inertia, const RobotState& state);
  void buildQp(double dt);

  const Matrix<double, Dynamic, Dynamic, Eigen::RowMajor>& buildHessianMat();
  const VectorXd& buildGVec(double gravity, const RobotState& state, const Matrix<double, Dynamic, 1>& traj);
  const Matrix<double, Dynamic, Dynamic, Eigen::RowMajor>& buildConstrainMat(double mu);
  const VectorXd& buildUpperBound(double f_max, const VectorXd& gait_table);
  const VectorXd& buildLowerBound();

  int horizon_;

  // Final QP Formation
  // 1/2 U^{-T} H U + U^{T} g
  Matrix<double, Dynamic, Dynamic, Eigen::RowMajor> h_;  // hessian Matrix
  VectorXd g_;                                           // g vector
  Matrix<double, Dynamic, Dynamic, Eigen::RowMajor> c_;  // constrain matrix
  VectorXd u_b_;                                         // upper bound
  VectorXd l_b_;                                         // lower bound

private:
  // State Space Model
  Matrix<double, STATE_DIM, STATE_DIM> a_c_;
  Matrix<double, STATE_DIM, ACTION_DIM> b_c_;
  Matrix<double, Dynamic, STATE_DIM> a_qp_;
  MatrixXd b_qp_;

  // Weight
  // L matrix: Diagonal matrix of weights for state deviations
  Eigen::DiagonalMatrix<double, Eigen::Dynamic, Eigen::Dynamic> l_;
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> alpha_;  // u cost
};

template <typename T>
T square(T a)
{
  return a * a;
}

/*!
 * Convert a quaternion to RPY.  Uses ZYX order (yaw-pitch-roll), but returns
 * angles in (roll, pitch, yaw).
 */
template <typename T>
Vec3<T> quatToRPY(const Eigen::Quaternion<T>& q)
{
  Vec3<T> rpy;

  T as = std::min(-2. * (q.x() * q.z() - q.w() * q.y()), .99999);
  rpy(2) =
      std::atan2(2 * (q.x() * q.y() + q.w() * q.z()), square(q.w()) + square(q.x()) - square(q.y()) - square(q.z()));
  rpy(1) = std::asin(as);
  rpy(0) =
      std::atan2(2 * (q.y() * q.z() + q.w() * q.x()), square(q.w()) - square(q.x()) - square(q.y()) + square(q.z()));
  return rpy;
}

}  // namespace cheetah_ros
