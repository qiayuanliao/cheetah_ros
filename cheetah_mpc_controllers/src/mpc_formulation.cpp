//
// Created by qiayuan on 2021/11/16.
//
// Refer: https://github.com/mit-biomimetics/Cheetah-Software
// Refer: https://github.com/google-research/motion_imitation

#include "cheetah_mpc_controllers/mpc_formulation.h"

#include <cheetah_common/math_utilities.h>

#include <unsupported/Eigen/MatrixFunctions>

namespace cheetah_ros
{
void MpcFormulation::setup(int horizon, const Matrix<double, STATE_DIM, 1>& weight, double alpha,
                           double final_cost_scale)
{
  horizon_ = horizon;
  final_cost_scale_ = final_cost_scale;
  // Resize
  a_c_.resize(STATE_DIM, STATE_DIM);
  b_c_.resize(STATE_DIM, ACTION_DIM);
  a_qp_.resize(STATE_DIM * horizon, Eigen::NoChange);
  b_qp_.resize(STATE_DIM * horizon, ACTION_DIM * horizon);
  l_.resize(STATE_DIM * horizon);
  alpha_.resize(12 * horizon_, 12 * horizon_);
  h_.resize(ACTION_DIM * horizon, ACTION_DIM * horizon);
  g_.resize(ACTION_DIM * horizon, Eigen::NoChange);
  a_.resize(5 * 4 * horizon, ACTION_DIM * horizon);
  ub_a_.resize(5 * 4 * horizon, Eigen::NoChange);
  lb_a_.resize(5 * 4 * horizon, Eigen::NoChange);
  // Set Zero
  a_c_.setZero();
  b_c_.setZero();
  a_qp_.setZero();
  b_qp_.setZero();
  l_.setZero();
  l_.diagonal() = weight.replicate(horizon, 1);
  l_.diagonal().block((horizon - 1) * STATE_DIM, 0, STATE_DIM, 1) *= final_cost_scale;
  alpha_.setIdentity();
  alpha_ = alpha * alpha_;
}

// Converts a vector to the skew symmetric matrix form. For an input vector
// [a, b, c], the output matrix would be:
//   [ 0, -c,  b]
//   [ c,  0, -a]
//   [-b,  a,  0]
Matrix3d convertToSkewSymmetric(const Vector3d& vec)
{
  Matrix3d skew_sym_mat;
  skew_sym_mat << 0, -vec(2), vec(1), vec(2), 0, -vec(0), -vec(1), vec(0), 0;
  return skew_sym_mat;
}

void MpcFormulation::buildStateSpace(double mass, const Matrix3d& inertia, const RobotState& state)
{
  Matrix3d angular_velocity_to_rpy_rate;
  Vector3d rpy = quatToRPY(state.quat_);
  double yaw_cos = std::cos(rpy(2));
  double yaw_sin = std::sin(rpy(2));
  angular_velocity_to_rpy_rate << yaw_cos, yaw_sin, 0, -yaw_sin, yaw_cos, 0, 0, 0, 1;

  Matrix<double, 3, 4> r_feet;
  for (int i = 0; i < 4; ++i)
    r_feet.col(i) = state.foot_pos_[i] - state.pos_;

  a_c_.block<3, 3>(0, 6) = angular_velocity_to_rpy_rate;

  a_c_(3, 9) = 1.;
  a_c_(4, 10) = 1.;
  a_c_(5, 11) = 1.;
  a_c_(11, 12) = 1.;

  Matrix3d inertia_world = angular_velocity_to_rpy_rate.transpose() * inertia * angular_velocity_to_rpy_rate;
  //  b contains non_zero elements only in row 6 : 12.
  for (int i = 0; i < 4; ++i)
  {
    b_c_.block<3, 3>(6, i * 3) = inertia_world.inverse() * convertToSkewSymmetric(r_feet.col(i));
    b_c_.block(9, i * 3, 3, 3) = Matrix<double, 3, 3>::Identity() / mass;
  }
}

void MpcFormulation::buildQp(double dt)
{
  // Convert model from continuous to discrete time
  Matrix<double, STATE_DIM + ACTION_DIM, STATE_DIM + ACTION_DIM> ab_c;
  ab_c.setZero();
  ab_c.block(0, 0, STATE_DIM, STATE_DIM) = a_c_;
  ab_c.block(0, STATE_DIM, STATE_DIM, ACTION_DIM) = b_c_;
  ab_c = dt * ab_c;
  Matrix<double, STATE_DIM + ACTION_DIM, STATE_DIM + ACTION_DIM> exp = ab_c.exp();
  Matrix<double, STATE_DIM, STATE_DIM> a_dt = exp.block(0, 0, STATE_DIM, STATE_DIM);
  Matrix<double, STATE_DIM, ACTION_DIM> b_dt = exp.block(0, STATE_DIM, STATE_DIM, ACTION_DIM);

  std::vector<Matrix<double, STATE_DIM, STATE_DIM>> power_mats;
  power_mats.resize(horizon_ + 1);
  for (auto& power_mat : power_mats)
    power_mat.setZero();
  power_mats[0].setIdentity();
  for (int i = 1; i < horizon_ + 1; i++)
    power_mats[i] = a_dt * power_mats[i - 1];

  for (int r = 0; r < horizon_; r++)
  {
    a_qp_.block(STATE_DIM * r, 0, STATE_DIM, STATE_DIM) = power_mats[r + 1];  // Adt.pow(r+1);

    for (int c = 0; c < horizon_; c++)
    {
      if (r >= c)
      {
        int a_num = r - c;
        b_qp_.block(STATE_DIM * r, ACTION_DIM * c, STATE_DIM, ACTION_DIM) = power_mats[a_num] * b_dt;
      }
    }
  }
}

const Matrix<double, Dynamic, Dynamic, Eigen::RowMajor>& MpcFormulation::buildHessianMat()
{
  h_ = /*2. * */ (b_qp_.transpose() * l_ * b_qp_ + alpha_);
  return h_;
}

const VectorXd& MpcFormulation::buildGVec(double gravity, const RobotState& state,
                                          const Matrix<double, Dynamic, 1>& traj)
{
  // Update x_0 and x_ref
  Matrix<double, STATE_DIM, 1> x_0;
  VectorXd x_ref(STATE_DIM * horizon_);

  Vector3d rpy = quatToRPY(state.quat_);
  x_0 << rpy(0), rpy(1), rpy(2), state.pos_, state.angular_vel_, state.linear_vel_, gravity;
  for (int i = 0; i < horizon_; i++)
    for (int j = 0; j < STATE_DIM - 1; j++)
      x_ref(STATE_DIM * i + j, 0) = traj[12 * i + j];

  g_ = /*2. * */ b_qp_.transpose() * l_ * (a_qp_ * x_0 - x_ref);
  return g_;
}

const Matrix<double, Dynamic, Dynamic, Eigen::RowMajor>& MpcFormulation::buildConstrainMat(double mu)
{
  a_.setZero();
  double mu_inv = 1.f / mu;
  Matrix<double, 5, 3> a_block;
  a_block << mu_inv, 0, 1., -mu_inv, 0, 1., 0, mu_inv, 1., 0, -mu_inv, 1., 0, 0, 1.;
  for (int i = 0; i < horizon_ * 4; i++)
    a_.block(i * 5, i * 3, 5, 3) = a_block;
  return a_;
}

const VectorXd& MpcFormulation::buildConstrainUpperBound(double f_max, const VectorXd& gait_table)
{
  for (int i = 0; i < horizon_; ++i)
  {
    for (int j = 0; j < 4; ++j)
    {
      const int row = (i * 4 + j) * 5;
      ub_a_(row) = BIG_VALUE;
      ub_a_(row + 1) = BIG_VALUE;
      ub_a_(row + 2) = BIG_VALUE;
      ub_a_(row + 3) = BIG_VALUE;
      ub_a_(row + 4) = f_max * gait_table(i * 4 + j);
    }
  }
  return ub_a_;
}

const VectorXd& MpcFormulation::buildConstrainLowerBound()
{
  lb_a_.setZero();
  return lb_a_;
}

}  // namespace cheetah_ros
