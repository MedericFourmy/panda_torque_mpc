#include <panda_torque_mpc/joint_space_ID_controller.h>

#include <cmath>
#include <memory>

#include <controller_interface/controller_base.h>
#include <pluginlib/class_list_macros.h>
#include <ros/ros.h>

#include <franka/robot_state.h>

namespace panda_torque_mpc {

bool JointSpaceIDController::init(hardware_interface::RobotHW* robot_hw,
                                           ros::NodeHandle& node_handle) {

  ///////////////////
  // Load parameters
  ///////////////////
  std::string arm_id;
  if (!node_handle.getParam("arm_id", arm_id)) {
    ROS_ERROR("JointSpaceIDController: Could not read parameter arm_id");
    return false;
  }

  std::vector<std::string> joint_names;
  if (!node_handle.getParam("joint_names", joint_names) || joint_names.size() != 7) {
    ROS_ERROR("JointSpaceIDController: Invalid or no joint_names parameters provided, aborting controller init!");
    return false;
  }

  if (!node_handle.getParam("Kp", Kp_)) {
    ROS_ERROR("JointSpaceIDController: Could not read parameter Kp");
    return false;
  }

  if (!node_handle.getParam("Kd", Kd_)) {
    ROS_ERROR("JointSpaceIDController: Could not read parameter Kd");
    return false;
  }

  std::vector<double> kp_gains;
  if (!node_handle.getParam("kp_gains", kp_gains) || kp_gains.size() != 7) {
    ROS_ERROR("JointSpaceIDController:  Invalid or no kp_gains parameters provided, aborting controller init!");
    return false;
  }
  kp_gains_ = Eigen::Map<Vector7d>(kp_gains.data());

  std::vector<double> kd_gains;
  if (!node_handle.getParam("kd_gains", kd_gains) || kd_gains_.size() != 7) {
    ROS_ERROR("JointSpaceIDController:  Invalid or no kd_gains parameters provided, aborting controller init!");
    return false;
  }
  kd_gains_ = Eigen::Map<Vector7d>(kd_gains.data());

  std::vector<double> delta_q;
  if (!node_handle.getParam("delta_q", delta_q) || delta_q_.size() != 7) {
    ROS_ERROR("JointSpaceIDController:  Invalid or no delta_q parameters provided, aborting controller init!");
    return false;
  }
  delta_q_ = Eigen::Map<Vector7d>(delta_q.data());

  std::vector<double> period_q;
  if (!node_handle.getParam("period_q", period_q) || period_q_.size() != 7) {
    ROS_ERROR("JointSpaceIDController:  Invalid or no period_q parameters provided, aborting controller init!");
    return false;
  }
  period_q_ = Eigen::Map<Vector7d>(period_q.data());

  double publish_rate(30.0);
  if (!node_handle.getParam("publish_rate", publish_rate)) {
    ROS_INFO_STREAM("JointSpaceIDController: publish_rate not found. Defaulting to " << publish_rate);
  }
  rate_trigger_ = franka_hw::TriggerRate(publish_rate);

  if (!node_handle.getParam("use_pinocchio", use_pinocchio_)) {
    ROS_ERROR_STREAM("JointSpaceIDController: Could not read parameter use_pinocchio");
  }

  if (!node_handle.getParam("alpha_dq_filter", alpha_dq_filter_)) {
    ROS_ERROR_STREAM("JointSpaceIDController: Could not read parameter alpha_dq_filter");
  }

  int idc;
  if (!node_handle.getParam("control_variant", idc)  || !(idc >= 0 && idc < 3)) {
    ROS_ERROR_STREAM("JointSpaceIDController: Invalid or no control_variant parameters provided, aborting controller init! control_variant: " << idc);
  }
  control_variant_ = static_cast<JointSpaceIDController::Variant>(idc);

  // Load panda model with pinocchio
  std::string urdf_path;
  if (!node_handle.getParam("urdf_path", urdf_path)) {
    ROS_ERROR("JointSpaceIDController: Could not read parameter urdf_path");
    return false;
  }
  pin::urdf::buildModel(urdf_path, model_pin_);
  std::cout << "model name: " << model_pin_.name << std::endl;
  data_pin_ = pin::Data(model_pin_);

  if ((model_pin_.nq != 7) || (model_pin_.name != "panda"))
  {
    ROS_ERROR_STREAM("Problem when loading the robot urdf");
    return false;
  }


  ///////////////////
  // Claim interfaces
  ///////////////////
  // Retrieve resource FrankaStateHandle
  auto* franka_state_interface = robot_hw->get<franka_hw::FrankaStateInterface>();
  if (franka_state_interface == nullptr) {
    ROS_ERROR("ModelPinocchioVsFrankaController: Could not get Franka state interface from hardware");
    return false;
  }
  try {
    franka_state_handle_ = std::make_unique<franka_hw::FrankaStateHandle>(franka_state_interface->getHandle(arm_id + "_robot"));
  } catch (const hardware_interface::HardwareInterfaceException& ex) {
    ROS_ERROR_STREAM("ModelPinocchioVsFrankaController: Exception getting franka state handle: " << ex.what());
    return false;
  }

  // Retrieve resource FrankaModelHandle
  auto* model_interface = robot_hw->get<franka_hw::FrankaModelInterface>();
  if (model_interface == nullptr) {
    ROS_ERROR_STREAM("ModelPinocchioVsFrankaController: Error getting model interface from hardware");
    return false;
  }
  try {
    franka_model_handle_ = std::make_unique<franka_hw::FrankaModelHandle>(model_interface->getHandle(arm_id + "_model"));
  } catch (hardware_interface::HardwareInterfaceException& ex) {
    ROS_ERROR_STREAM("ModelPinocchioVsFrankaController: Exception getting model handle from interface: " << ex.what());
    return false;
  }

  // Retrieve resource FrankaModelHandle
  auto* effort_joint_interface = robot_hw->get<hardware_interface::EffortJointInterface>();
  if (effort_joint_interface == nullptr) {
    ROS_ERROR_STREAM("JointSpaceIDController: Error getting effort joint interface from hardware");
    return false;
  }
  for (size_t i = 0; i < 7; ++i) {
    try {
      joint_handles_.push_back(effort_joint_interface->getHandle(joint_names[i]));
    } catch (const hardware_interface::HardwareInterfaceException& ex) {
      ROS_ERROR_STREAM("JointSpaceIDController: Exception getting joint handles: " << ex.what());
      return false;
    }
  }

  configurations_publisher_.init(node_handle, "joint_configurations_comparison", 1);
  velocities_publisher_.init(node_handle, "joint_velocities_comparison", 1);
  torques_publisher_.init(node_handle, "joint_torques_comparison", 1);

  dq_filtered_ = Vector7d::Zero();

  return true;
}

void JointSpaceIDController::starting(const ros::Time& t0) {
  ROS_INFO_STREAM("JointSpaceIDController::starting");
  t_init_ = t0;
  q_init_ = Eigen::Map<const Vector7d>(franka_state_handle_->getRobotState().q.data());
}


void JointSpaceIDController::update(const ros::Time& t, const ros::Duration& period) {

  // Time since start of the controller
  double Dt = (t - t_init_).toSec();

  // compute desired configuration and configuration velocity
  Vector7d q_r, dq_r, ddq_r;
  compute_sinusoid_joint_reference(delta_q_, period_q_, q_init_, Dt, q_r, dq_r, ddq_r);

  // Retrieve current measured robot state
  franka::RobotState robot_state = franka_state_handle_->getRobotState();  // return a const& of RobotState object -> not going to be modified
  Eigen::Map<Vector7d> q_m(robot_state.q.data());
  Eigen::Map<Vector7d> dq_m(robot_state.dq.data());
  Eigen::Map<Vector7d> tau_m(robot_state.tau_J.data());  // measured torques -> naturally contains gravity torque
  Vector7d zero7 = Vector7d::Zero();

  // filter the joint velocity measurements
  dq_filtered_ = (1 - alpha_dq_filter_) * dq_filtered_ + alpha_dq_filter_ * dq_m;

  /** 
   * Lagrangian dynamics
   * 
   * M*ddq + C(q,dq)*dq + g(q) = tau + J^T*f
   */
  // libfranka dynamics
  std::array<double, 7> coriolis_fra_arr = franka_model_handle_->getCoriolis();
  std::array<double, 7> gravity_fra_arr = franka_model_handle_->getGravity();
  std::array<double, 49> M_fra_arr = franka_model_handle_->getMass();
  // Eigen and Franka use Column-Major storage order (see model_pinocchio_vs_frank_controller)
  Eigen::Map<Vector7d> coriolis_fra(coriolis_fra_arr.data());
  Eigen::Map<Vector7d> gravity_fra(gravity_fra_arr.data());
  Eigen::Map<Matrix7d> M_fra(M_fra_arr.data()); 

  // Common to all flavors variants
  Vector7d e = q_m - q_r;
  Vector7d de = dq_filtered_ - dq_r;

  // Compute desired torque
  //  -> should NOT include gravity terms since taken care of by internal Franka controller
  Vector7d tau_d;
  switch (control_variant_) {
    case Variant::IDControl:
      ROS_INFO_STREAM("Variant::IDControl, pinocchio: " << use_pinocchio_);
      /**
       * tau_cmd = M * ddq_r + h(q_m, dq_m) + M * (- kp_arr * e - kd_arr * de)
       *         = M * (ddq_r - kp_arr * e - kd_arr * de) + h(q_m, dq_m)
       *         = rnea(q_m, dq_m, ddq_r - kp_arr * e - kd_arr * de)
       * 
       * Use gain arrays to way the gains according to the inertia the 
       *    -> need higher gains at the root of the robot
       * The best way to compute these relative scalings is to use the mass matrix like in IDControl
       * unless the model is not accurate enough
      */
      pin::rnea(model_pin_, data_pin_, q_m, dq_m, ddq_r - Kp_ * e - Kd_ * de); // data.tau (but not data.g), joint torques
      pin::computeGeneralizedGravity(model_pin_, data_pin_, q_m);  // data.g == generalized gravity

      // For pinocchio, substract the gravity term from rnea torque to get only centrifugal + Coriolis
      // For pinocchio, feedback is taken into account in rnea computation
      if(use_pinocchio_)
        tau_d = data_pin_.tau - data_pin_.g;
      else
        tau_d = M_fra * (ddq_r - Kp_*e - Kd_ * de) + coriolis_fra;
      
      break;
    case Variant::IDControlSimplified:
    {
      ROS_INFO_STREAM("Variant::IDControlSimplified, pinocchio: " << use_pinocchio_);
      /**
       * tau_cmd = (M*ddq_r + h(q_m, dq_m)) - kp_arr * e - kd_arr * de
       *         = rnea(q_m, dq_m, ddq_r) - kp_arr * e - kd_arr * de
       * 
       * Use gain arrays to way the gains according to the inertia the 
       *    -> need higher gains at the root of the robot
       * The best way to compute these relative scalings is to use the mass matrix like in IDControl
       * unless the model is not accurate enough
      */

      pin::rnea(model_pin_, data_pin_, q_m, dq_m, ddq_r); // data.tau (but not data.g), joint torques
      pin::computeGeneralizedGravity(model_pin_, data_pin_, q_m);  // data.g == generalized gravity

      // For pinocchio, substract the gravity term from rnea torque to get only centrifugal + Coriolis
      Vector7d tau_feedback = - kp_gains_.cwiseProduct(e) - kd_gains_.cwiseProduct(de); 
      if(use_pinocchio_)
        tau_d = data_pin_.tau - data_pin_.g + tau_feedback;
      else
        tau_d = M_fra * ddq_r + coriolis_fra + tau_feedback;
      
      break;
    }
    case Variant::PDGravity:
      ROS_INFO_STREAM("Variant::PDGravity, pinocchio: " << use_pinocchio_);
      /** 
       * tau_cmd = (g(q)) - kp_arr * e - kd_arr * de
       * 
       * Rely on the gravity compensation implemented in Panda + feedback torque
      */
      tau_d = - kp_gains_.cwiseProduct(e) - kd_gains_.cwiseProduct(de); 

      break;
  }

  // Maximum torque difference with a sampling rate of 1 kHz. The maximum torque rate is
  // 1000 * (1 / sampling_time).
  Vector7d tau_d_saturated = saturateTorqueRate(tau_d, Eigen::Map<Vector7d>(robot_state.tau_J_d.data()));

  // Send Torque Command
  for (size_t i = 0; i < 7; ++i) {
    joint_handles_[i].setCommand(tau_d_saturated[i]);
  }


  ///////////////////
  // Publish logs
  if (rate_trigger_() && torques_publisher_.trylock()) {

    /**
     * It seems the conventions are flipped between measured and commanded torques (action/reaction?)
     * So we artificially flip the measured torques values to be able to compare them with commanded torques.
     */
    tau_m = -tau_m;

    Vector7d q_error = last_q_r_ - q_m;
    Vector7d dq_error = last_dq_r_ - dq_m;
    Vector7d tau_error = last_tau_d_ - tau_m;

    double q_error_rms = std::sqrt(q_error.array().square().sum()) / 7.0;
    double dq_error_rms = std::sqrt(q_error.array().square().sum()) / 7.0;
    double tau_error_rms = std::sqrt(q_error.array().square().sum()) / 7.0;
    configurations_publisher_.msg_.root_mean_square_error = q_error_rms;
    velocities_publisher_.msg_.root_mean_square_error = dq_error_rms;
    torques_publisher_.msg_.root_mean_square_error = tau_error_rms;
    
    for (size_t i = 0; i < 7; ++i) {
      // Joint config
      configurations_publisher_.msg_.q_commanded[i] = last_q_r_[i];
      configurations_publisher_.msg_.q_error[i] = q_error[i];
      configurations_publisher_.msg_.q_measured[i] = q_m[i];

      // Joint velocities
      velocities_publisher_.msg_.dq_commanded[i] = last_dq_r_[i];
      velocities_publisher_.msg_.dq_error[i] = dq_error[i];
      velocities_publisher_.msg_.dq_measured[i] = dq_m[i];

      // Joint torque
      torques_publisher_.msg_.tau_commanded[i] = last_tau_d_[i];
      torques_publisher_.msg_.tau_error[i] = tau_error[i];
      torques_publisher_.msg_.tau_measured[i] = tau_m[i];
    }

    configurations_publisher_.unlockAndPublish();
    velocities_publisher_.unlockAndPublish();
    torques_publisher_.unlockAndPublish();
  }


  // Store previous desired/reference values
  last_q_r_ = q_r; 
  last_dq_r_ = dq_r; 

  last_tau_d_ = tau_d_saturated + gravity_fra;
}

void JointSpaceIDController::compute_sinusoid_joint_reference(const Vector7d& delta_q, const Vector7d& period_q, const Vector7d& q0, double t,
                                                             Vector7d& q_r, Vector7d& dq_r, Vector7d& ddq_r){ 
  for (size_t i = 0; i < 7; i++) {
    double omg = 2*M_PI/period_q[i];
    double A = - 0.5 * delta_q[i];
    double C = q0[i] + 0.5 * delta_q[i];

    q_r[i]   =          A*cos(omg*t) + C;
    dq_r[i]  =     -omg*A*sin(omg*t);
    ddq_r[i] = -omg*omg*A*cos(omg*t);  // non null initial acceleration!! needs to be dampened (e.g. torque staturation)
  }

}

Vector7d JointSpaceIDController::saturateTorqueRate(
    const Vector7d& tau_d,
    const Vector7d& tau_J_d) {  // NOLINT (readability-identifier-naming)
  Vector7d tau_d_saturated{};
  for (size_t i = 0; i < 7; i++) {
    double difference = tau_d[i] - tau_J_d[i];
    tau_d_saturated[i] = tau_J_d[i] + std::max(std::min(difference, kDeltaTauMax), -kDeltaTauMax);
  }
  return tau_d_saturated;
}


void JointSpaceIDController::stopping(const ros::Time& t0) {
  ROS_INFO_STREAM("JointSpaceIDController::stopping");
}

}  // namespace panda_torque_mpc

PLUGINLIB_EXPORT_CLASS(panda_torque_mpc::JointSpaceIDController,
                       controller_interface::ControllerBase)
