//
// Created by qiayuan on 2021/11/16.
//
#pragma once
#include "state_estimate.h"

#include <vector>
#include <Eigen/Dense>
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace unitree_ros
{
using Eigen::AngleAxisd;
using Eigen::Dynamic;
using Eigen::Matrix;
using Eigen::Matrix3d;
using Eigen::MatrixXd;
using Eigen::Quaterniond;
using Eigen::Vector3d;
using Eigen::VectorXd;

class MpcFormulation
{
public:
  static constexpr int STATE_DIM = 13;   // 6 dof pose + 6 dof velocity + 1 gravity.
  static constexpr int ACTION_DIM = 12;  // 4 ground reaction force.

  struct Config
  {
    double dt_;
    double gravity_;
    double mu_;
    double mass_;
    double f_max_;
    int horizon_;
    Matrix3d inertia_;
    double weight_[12];
  };

  void build(const StateEstimateBase::State& state, const Matrix3d& feet_pos, const Matrix<double, Dynamic, 1>& x_des);

private:
  void buildStateSpace(const Matrix3d& rot_yaw, const Matrix<double, 3, 4>& r_feet,
                       Matrix<double, STATE_DIM, STATE_DIM>& a, Matrix<double, STATE_DIM, ACTION_DIM>& b);
  void buildQp(const Matrix<double, STATE_DIM, STATE_DIM>& a_c, const Matrix<double, STATE_DIM, ACTION_DIM>& b_c,
               Matrix<double, Dynamic, STATE_DIM>& a_qp, Matrix<double, Dynamic, Dynamic>& b_qp);

  // Final QP Formation
  // 1/2 U^{-T} H U + U^{T} g
  Matrix<double, Dynamic, Dynamic> h_;  // hessian Matrix
  Matrix<double, Dynamic, 1> g_;        // g vector
  Matrix<double, Dynamic, 1> c_;        // constrain matrix
  Matrix<double, Dynamic, 1> u_b_;      // upper bound
  Matrix<double, Dynamic, 1> l_b_;      // lower bound

  Config config_;
};

}  // namespace unitree_ros