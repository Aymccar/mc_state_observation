/* Copyright 2017-2020 CNRS-AIST JRL, CNRS-UM LIRMM */

#include <mc_control/MCController.h>
#include <mc_observers/ObserverMacros.h>
#include <mc_rtc/io_utils.h>
#include <mc_rtc/logging.h>
#include <mc_state_observation/MCKineticsObserver.h>
#include <mc_state_observation/gui_helpers.h>

#include <RBDyn/CoM.h>
#include <RBDyn/FA.h>
#include <RBDyn/FK.h>
#include <RBDyn/FV.h>

#include <iostream>

namespace mc_state_observation
{
MCKineticsObserver::MCKineticsObserver(const std::string & type, double dt)
: mc_observers::Observer(type, dt), observer_(4, 2)
{
  observer_.setSamplingTime(dt);
}

///////////////////////////////////////////////////////////////////////
/// --------------------------Core functions---------------------------
///////////////////////////////////////////////////////////////////////

void MCKineticsObserver::configure(const mc_control::MCController & ctl, const mc_rtc::Configuration & config)
{
  robot_ = config("robot", ctl.robot().name());
  IMUs_ = config("imuSensor", ctl.robot().bodySensors());
  config("debug", debug_);
  config("verbose", verbose_);

  config("withOdometry", withOdometry_);
  config("withContactsDetection", withContactsDetection_);
  config("withUnmodeledWrench", withUnmodeledWrench_);
  config("withGyroBias", withGyroBias_);

  observer_.setWithUnmodeledWrench(withUnmodeledWrench_);
  observer_.setWithGyroBias(withGyroBias_);
  observer_.useFiniteDifferencesJacobians(config("withFiniteDifferences"));
  so::Vector dx(observer_.getStateSize());
  dx.setConstant(static_cast<double>(config("finiteDifferenceStep")));
  observer_.setFiniteDifferenceStep(dx);
  observer_.setWithAccelerationEstimation(config("withAccelerationEstimation"));
  observer_.useRungeKutta(config("withRungeKutta"));

  config("flexStiffness", flexStiffness_);
  config("flexDamping", flexDamping_);

  zeroPose_.translation().setZero();
  zeroPose_.rotation().setIdentity();
  zeroMotion_.linear().setZero();
  zeroMotion_.angular().setZero();

  // Initial State
  statePositionInitCovariance_ = static_cast<so::Vector3>(config("statePositionInitVariance")).matrix().asDiagonal();
  stateOriInitCovariance_ = static_cast<so::Vector3>(config("stateOriInitVariance")).matrix().asDiagonal();
  stateLinVelInitCovariance_ = static_cast<so::Vector3>(config("stateLinVelInitVariance")).matrix().asDiagonal();
  stateAngVelInitCovariance_ = static_cast<so::Vector3>(config("stateAngVelInitVariance")).matrix().asDiagonal();
  gyroBiasInitCovariance_.setZero();
  unmodeledWrenchInitCovariance_.setZero();
  contactInitCovarianceFirstContacts_.setZero();
  contactInitCovarianceFirstContacts_.block<3, 3>(0, 0) =
      static_cast<so::Vector3>(config("contactPositionInitVarianceFirstContacts")).matrix().asDiagonal();
  contactInitCovarianceFirstContacts_.block<3, 3>(3, 3) =
      static_cast<so::Vector3>(config("contactOriInitVarianceFirstContacts")).matrix().asDiagonal();
  contactInitCovarianceFirstContacts_.block<3, 3>(6, 6) =
      static_cast<so::Vector3>(config("contactForceInitVarianceFirstContacts")).matrix().asDiagonal();
  contactInitCovarianceFirstContacts_.block<3, 3>(9, 9) =
      static_cast<so::Vector3>(config("contactTorqueInitVarianceFirstContacts")).matrix().asDiagonal();

  contactInitCovarianceNewContacts_.setZero();
  contactInitCovarianceNewContacts_.block<3, 3>(0, 0) =
      static_cast<so::Vector3>(config("contactPositionInitVarianceNewContacts")).matrix().asDiagonal();
  contactInitCovarianceNewContacts_.block<3, 3>(3, 3) =
      static_cast<so::Vector3>(config("contactOriInitVarianceNewContacts")).matrix().asDiagonal();
  contactInitCovarianceNewContacts_.block<3, 3>(6, 6) =
      static_cast<so::Vector3>(config("contactForceInitVarianceNewContacts")).matrix().asDiagonal();
  contactInitCovarianceNewContacts_.block<3, 3>(9, 9) =
      static_cast<so::Vector3>(config("contactTorqueInitVarianceNewContacts")).matrix().asDiagonal();

  // Process //
  statePositionProcessCovariance_ =
      static_cast<so::Vector3>(config("statePositionProcessVariance")).matrix().asDiagonal();
  stateOriProcessCovariance_ = static_cast<so::Vector3>(config("stateOriProcessVariance")).matrix().asDiagonal();
  stateLinVelProcessCovariance_ = static_cast<so::Vector3>(config("stateLinVelProcessVariance")).matrix().asDiagonal();
  stateAngVelProcessCovariance_ = static_cast<so::Vector3>(config("stateAngVelProcessVariance")).matrix().asDiagonal();
  gyroBiasProcessCovariance_.setZero();
  unmodeledWrenchProcessCovariance_.setZero();

  contactProcessCovariance_.setZero();
  contactProcessCovariance_.block<3, 3>(0, 0) =
      static_cast<so::Vector3>(config("contactPositionProcessVariance")).matrix().asDiagonal();
  contactProcessCovariance_.block<3, 3>(3, 3) =
      static_cast<so::Vector3>(config("contactOrientationProcessVariance")).matrix().asDiagonal();
  contactProcessCovariance_.block<3, 3>(6, 6) =
      static_cast<so::Vector3>(config("contactForceProcessVariance")).matrix().asDiagonal();
  contactProcessCovariance_.block<3, 3>(9, 9) =
      static_cast<so::Vector3>(config("contactTorqueProcessVariance")).matrix().asDiagonal();

  // Unmodeled Wrench //
  if(withUnmodeledWrench_)
  {
    // initial
    unmodeledWrenchInitCovariance_.block<3, 3>(0, 0) =
        static_cast<so::Vector3>(config("unmodeledForceInitVariance")).matrix().asDiagonal();
    unmodeledWrenchInitCovariance_.block<3, 3>(3, 3) =
        static_cast<so::Vector3>(config("unmodeledTorqueInitVariance")).matrix().asDiagonal();

    // process
    unmodeledWrenchProcessCovariance_.block<3, 3>(0, 0) =
        static_cast<so::Vector3>(config("unmodeledForceProcessVariance")).matrix().asDiagonal();
    unmodeledWrenchProcessCovariance_.block<3, 3>(3, 3) =
        static_cast<so::Vector3>(config("unmodeledTorqueProcessVariance")).matrix().asDiagonal();
  }
  // Gyrometer Bias
  if(withGyroBias_)
  {
    gyroBiasInitCovariance_ = static_cast<so::Vector3>(config("gyroBiasInitVariance")).matrix().asDiagonal();
    gyroBiasProcessCovariance_ = static_cast<so::Vector3>(config("gyroBiasProcessVariance")).matrix().asDiagonal();
  }

  // Sensor //
  positionSensorCovariance_ = static_cast<so::Vector3>(config("positionSensorVariance")).matrix().asDiagonal();
  orientationSensorCoVariance_ = static_cast<so::Vector3>(config("orientationSensorVariance")).matrix().asDiagonal();
  acceleroSensorCovariance_ = static_cast<so::Vector3>(config("acceleroSensorVariance")).matrix().asDiagonal();
  gyroSensorCovariance_ = static_cast<so::Vector3>(config("gyroSensorVariance")).matrix().asDiagonal();
  contactSensorCovariance_.setZero();
  contactSensorCovariance_.block<3, 3>(0, 0) =
      static_cast<so::Vector3>(config("forceSensorVariance")).matrix().asDiagonal();
  contactSensorCovariance_.block<3, 3>(3, 3) =
      static_cast<so::Vector3>(config("torqueSensorVariance")).matrix().asDiagonal();

  observer_.setAllCovariances(statePositionInitCovariance_, stateOriInitCovariance_, stateLinVelInitCovariance_,
                              stateAngVelInitCovariance_, gyroBiasInitCovariance_, unmodeledWrenchInitCovariance_,
                              contactInitCovarianceFirstContacts_, statePositionProcessCovariance_,
                              stateOriProcessCovariance_, stateLinVelProcessCovariance_, stateAngVelProcessCovariance_,
                              gyroBiasProcessCovariance_, unmodeledWrenchProcessCovariance_, contactProcessCovariance_,
                              positionSensorCovariance_, orientationSensorCoVariance_, acceleroSensorCovariance_,
                              gyroSensorCovariance_, contactSensorCovariance_);
}

void MCKineticsObserver::reset(const mc_control::MCController & ctl)
{
  simStarted_ = false;
  ekfIsSet_ = false;

  // mapIMUs_.setMaxElements(2);
  // mapContacts_.setMaxElements(4);

  const auto & robot = ctl.robot(robot_);
  const auto & realRobot = ctl.realRobot(robot_);
  const auto & realRobotModule = realRobot.module();

  rbd::MultiBodyGraph mergeMbg(realRobotModule.mbg);
  std::map<std::string, std::vector<double>> jointPosByName;
  for(int i = 0; i < realRobotModule.mb.nrJoints(); ++i)
  {
    auto jointName = realRobotModule.mb.joint(i).name();
    auto jointIndex = static_cast<unsigned long>(realRobotModule.mb.jointIndexByName(jointName));
    jointPosByName[jointName] = realRobotModule.mbc.q[jointIndex];
  }

  std::vector<std::string> rootJoints = {};
  int nbJoints = static_cast<int>(realRobot.mb().joints().size());
  for(int i = 0; i < nbJoints; ++i)
  {
    if(realRobot.mb().predecessor(i) == 0)
    {
      rootJoints.push_back(realRobot.mb().joint(i).name());
    }
  }
  for(const auto & joint : rootJoints)
  {
    if(!realRobot.hasJoint(joint))
    {
      mc_rtc::log::error_and_throw<std::runtime_error>("Robot does not have a joint named {}", joint);
    }
    mergeMbg.mergeSubBodies(realRobotModule.mb.body(0).name(), joint, jointPosByName);
  }

  inertiaWaist_ = mergeMbg.nodeByName(realRobotModule.mb.body(0).name())->body.inertia();
  mass(ctl.realRobot(robot_).mass());
  flexStiffness(flexStiffness_);
  flexDamping(flexDamping_);

  for(auto forceSensor : realRobot.forceSensors())
  {
    isExternalWrench_[forceSensor.name()] = true;
  }

  if(debug_)
  {
    mc_rtc::log::info("inertiaWaist = {}", inertiaWaist_);
    mc_rtc::log::info("flexStiffness_ = {}", flexStiffness_);
    mc_rtc::log::info("flexDamping_ = {}", flexDamping_);
  }

  my_robots_ = mc_rbdyn::Robots::make();
  my_robots_->robotCopy(robot, robot.name());
  my_robots_->robotCopy(realRobot, "inputRobot");
  ctl.gui()->addElement(
      {"Robots"},
      mc_rtc::gui::Robot("MCKineticsobserver", [this]() -> const mc_rbdyn::Robot & { return my_robots_->robot(); }));
  ctl.gui()->addElement({"Robots"},
                        mc_rtc::gui::Robot("Real", [&ctl]() -> const mc_rbdyn::Robot & { return ctl.realRobot(); }));
}

bool MCKineticsObserver::run(const mc_control::MCController & ctl)
{
  std::cout << "\033[1;31m" << std::endl << "New iteration: " << std::endl << "\033[0m\n";

  const auto & robot = ctl.robot(robot_);
  const auto & realRobot = ctl.realRobot(robot_);
  auto & inputRobot = my_robots_->robot("inputRobot");
  auto & logger = (const_cast<mc_control::MCController &>(ctl)).logger();

  inputRobot.mbc() = realRobot.mbc();
  inputRobot.mb() = realRobot.mb();

  inputRobot.posW(zeroPose_);
  inputRobot.velW(zeroMotion_);
  inputRobot.accW(zeroMotion_);

  findContacts(ctl); // retrieves the list of contacts and set simStarted to true once a contact is detected

  if(!simStarted_) // this allows to ignore the first step on which the contacts are still not detected
                   // and to avoid the jump of the com's position between this step and the following one
  {
    return true;
  }

  /** Center of mass (assumes FK, FV and FA are already done) **/

  worldCoMKine_.position = inputRobot.com();
  worldCoMKine_.linVel = inputRobot.comVelocity();
  worldCoMKine_.linAcc = inputRobot.comAcceleration();

  std::cout << std::endl << "worldCoMKine_ : " << std::endl << worldCoMKine_ << std::endl;

  observer_.setCenterOfMass(worldCoMKine_.position(), worldCoMKine_.linVel(), worldCoMKine_.linAcc());

  /** Contacts
   * Note that when we use force sensors, this should be the position of the force sensor!
   */
  updateContacts(robot, inputRobot, contactsFound_, logger);

  // std::cout << std::endl << "Time: " << std::endl << observer_.getEKF().getCurrentTime() << std::endl;

  /** Accelerometers **/
  updateIMUs(robot, inputRobot);

  /** Inertias **/
  /** TODO : Merge inertias into CoM inertia and/or get it from fd() **/
  // Eigen::Vector6d inertia;
  // Eigen::Matrix3d inertiaAtOrigin =
  //     sva::inertiaToOrigin(inertiaWaist_.inertia(), mass_, inputRobot.com(), Eigen::Matrix3d::Identity().eval());
  // inertia << inertiaAtOrigin(0, 0), inertiaAtOrigin(1, 1), inertiaAtOrigin(2, 2), inertiaAtOrigin(0, 1),
  //     inertiaAtOrigin(0, 2), inertiaAtOrigin(1, 2);
  /*
  observer_.setCoMAngularMomentum(
      fbWorldKine.orientation.toMatrix3() * sigmaWorld,
      fbWorldKine.orientation.toMatrix3()
          * ((fbWorldKine_.orientation.toMatrix3() * fbWorldKine.angVel()).cross(sigmaWorld)
             + rbd::computeCentroidalMomentumDot(inputRobot.mb(), inputRobot.mbc(), inputRobot.com(),
  inputRobot.comVelocity()).moment()));
             */
  observer_.setCoMAngularMomentum(
      rbd::computeCentroidalMomentum(inputRobot.mb(), inputRobot.mbc(), inputRobot.com()).moment());
  /*
  std::cout << std::endl << "diff momentum: " << observer_.getAngularMomentum()() << std::endl;
  std::cout << std::endl
            << "diff momentum_d: "
            << observer_.getAngularMomentumDot()()
                   - (fbWorldKine_.orientation.toMatrix3()
                      * ((worldFbKine_.orientation.toMatrix3() * fbWorldKine_.angVel()).cross(sigmaWorld)
                         + rbd::computeCentroidalMomentumDot(inputRobot.mb(), inputRobot.mbc(), inputRobot.com(),
                                                             inputRobot.comVelocity())
                               .moment()))
            << std::endl;
  */
  observer_.setCoMInertiaMatrix(so::Matrix3(
      inertiaWaist_.inertia() + observer_.getMass() * so::kine::skewSymmetric2(observer_.getCenterOfMass()())));
  /* Step once, and return result */

  if(!ekfIsSet_) // the ekf is not updated, which means that it still has the initial values
  {
    plotVariablesBeforeUpdate(logger);
  }

  res_ = observer_.update();
  std::cout << std::endl << "state : " << std::endl << observer_.getCurrentStateVector() << std::endl;
  if(!ekfIsSet_)
  {
    plotVariablesAfterUpdate(logger);
  }

  ekfIsSet_ = true;

  /* Debug */
  robotImuOri_0 =
      so::KineticsObserver::Orientation(
          so::Matrix3(robot.bodyPosW(robot.bodySensor(mapIMUs_.getNameFromNum(0)).parentBody()).rotation().transpose()))
          .inverse()
          .toQuaternion();

  robotImuOri_1 =
      so::KineticsObserver::Orientation(
          so::Matrix3(robot.bodyPosW(robot.bodySensor(mapIMUs_.getNameFromNum(1)).parentBody()).rotation().transpose()))
          .inverse()
          .toQuaternion();
  realRobotImuOri_0 =
      so::KineticsObserver::Orientation(
          so::Matrix3(
              realRobot.bodyPosW(realRobot.bodySensor(mapIMUs_.getNameFromNum(0)).parentBody()).rotation().transpose()))
          .inverse()
          .toQuaternion();

  realRobotImuOri_1 =
      so::KineticsObserver::Orientation(
          so::Matrix3(
              realRobot.bodyPosW(realRobot.bodySensor(mapIMUs_.getNameFromNum(1)).parentBody()).rotation().transpose()))
          .inverse()
          .toQuaternion();

  robotFbOri_ =
      so::KineticsObserver::Orientation(so::Matrix3(robot.posW().rotation().transpose())).inverse().toQuaternion();

  realRobotFbOri_ =
      so::KineticsObserver::Orientation(so::Matrix3(realRobot.posW().rotation().transpose())).inverse().toQuaternion();

  robotPosImuInFB_0 = robot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().rotation().transpose()
                      * robot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().translation();
  robotPosImuInFB_1 = robot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().rotation().transpose()
                      * robot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().translation();

  realRobotPosImuInFB_0 = realRobot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().rotation().transpose()
                          * realRobot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().translation();
  realRobotPosImuInFB_1 = realRobot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().rotation().transpose()
                          * realRobot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().translation();

  robotTilt_0 =
      robot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().rotation() * robot.posW().rotation() * so::cst::gravity;
  robotTilt_1 =
      robot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().rotation() * robot.posW().rotation() * so::cst::gravity;
  realRobotTilt_0 = realRobot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().rotation() * realRobot.posW().rotation()
                    * so::cst::gravity;
  realRobotTilt_1 = realRobot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().rotation() * realRobot.posW().rotation()
                    * so::cst::gravity;

  realRobot_centroidImuOri_0 = observer_.getGlobalCentroidKinematics().orientation.inverse()
                               * so::kine::Orientation(realRobot.bodySensor(mapIMUs_.getNameFromNum(0)).orientation());

  realRobot_centroidImuOri_1 = observer_.getGlobalCentroidKinematics().orientation.inverse()
                               * so::kine::Orientation(realRobot.bodySensor(mapIMUs_.getNameFromNum(1)).orientation());

  /* Core */
  so::kine::Kinematics fbFb; // "Zero" Kinematics
  fbFb.setZero(so::kine::Kinematics::Flags::all);

  so::kine::Kinematics mcko_K_0_fb(observer_.getGlobalKinematicsOf(
      fbFb)); // Floating base in the 'real' world frame. The resulting state kinematics are used here
  X_0_fb_.rotation() = mcko_K_0_fb.orientation.toMatrix3().transpose();
  X_0_fb_.translation() = mcko_K_0_fb.position();

  std::cout << std::endl << "fbFb : " << std::endl << fbFb << std::endl;
  std::cout << std::endl << "mcko_K_0_fb : " << std::endl << mcko_K_0_fb << std::endl;
  std::cout << std::endl
            << "getGlobalCentroidKinematics : " << std::endl
            << observer_.getGlobalCentroidKinematics() << std::endl;

  MCKOrobotTilt_0 =
      robot.bodySensor(mapIMUs_.getNameFromNum(0)).X_b_s().rotation() * X_0_fb_.rotation() * so::cst::gravity;
  MCKOrobotTilt_1 =
      robot.bodySensor(mapIMUs_.getNameFromNum(1)).X_b_s().rotation() * X_0_fb_.rotation() * so::cst::gravity;

  /* Bring velocity of the IMU to the origin of the joint : we want the
   * velocity of joint 0, so stop one before the first joint */

  v_fb_0_.angular() = mcko_K_0_fb.angVel(); //  X_0_fb_.rotation() = mcko_K_0_fb.orientation.toMatrix3().transpose()
  v_fb_0_.linear() = mcko_K_0_fb.linVel();

  a_fb_0_.angular() = mcko_K_0_fb.angAcc(); //  X_0_fb_.rotation() = mcko_K_0_fb.orientation.toMatrix3().transpose()
  a_fb_0_.linear() = mcko_K_0_fb.linAcc();

  /* Updates of the logged variables */
  std::cout << std::endl << "simulating measurements: " << std::endl;
  correctedMeasurements_ = observer_.getEKF().getSimulatedMeasurement(
      observer_.getEKF().getCurrentTime()); // Used only in the logger as debugging help
  globalCentroidKinematics_ = observer_.getGlobalCentroidKinematics(); // Used only in the logger as debugging help

  predictedGlobalCentroidState_ =
      observer_.getPredictedGlobalCentroidState(); // Used only in the logger as debugging help
  predictedAccelerometersGravityComponent_ =
      observer_.getPredictedAccelerometersGravityComponent(); // Used only in the logger as debugging help
  predictedWorldIMUsLinAcc_ =
      observer_.getPredictedAccelerometersLinAccComponent(); // Used only in the logger as debugging help
  predictedAccelerometers_ = observer_.getPredictedAccelerometers(); // Used only in the logger as debugging help

  /* Update of the visual representation (only a visual feature) of the observed robot */
  my_robots_->robot().mbc().q = ctl.realRobot().mbc().q;

  /* Update of the observed robot */
  update(my_robots_->robot());

  return true;
}

///////////////////////////////////////////////////////////////////////
/// -------------------------Called functions--------------------------
///////////////////////////////////////////////////////////////////////

/*
void MCKineticsObserver::updateWorldFbKineAndViceVersa(const mc_rbdyn::Robot & inputRobot)
{
  const sva::PTransformd & worldFbPose = inputRobot.posW();
  const sva::MotionVecd & worldFbVel = inputRobot.velW();
  const sva::MotionVecd & worldFbAcc = inputRobot.accW();

  worldFbKine_.position = worldFbPose.translation();
  worldFbKine_.orientation = so::Matrix3(worldFbPose.rotation().transpose());
  worldFbKine_.linVel = worldFbVel.linear();
  worldFbKine_.angVel = worldFbVel.angular();
  worldFbKine_.linAcc = worldFbAcc.linear();
  worldFbKine_.angAcc = worldFbAcc.angular();

  fbWorldKine_ = worldFbKine_.getInverse();
}
*/

void MCKineticsObserver::initObserverStateVector(const mc_rbdyn::Robot & robot)
{
  so::kine::Orientation initOrientation;
  initOrientation.setZeroRotation<so::Quaternion>();
  Eigen::VectorXd initStateVector;
  initStateVector = Eigen::VectorXd::Zero(observer_.getStateSize());

  initStateVector.segment<int(observer_.sizePos)>(observer_.posIndex()) =
      robot.com(); // position of the centroid in fb = pos com in world - pos
                   // fb in world expressed in the frame of fb
  initStateVector.segment<int(observer_.sizeOri)>(observer_.oriIndex()) = initOrientation.toVector4();
  initStateVector.segment<int(observer_.sizeLinVel)>(observer_.linVelIndex()) =
      robot.comVelocity(); // position of the centroid in fb = pos com in world -
                           // pos fb in world expressed in the frame of fb

  observer_.setInitWorldCentroidStateVector(initStateVector);
}

void MCKineticsObserver::update(mc_control::MCController & ctl) // this function is called by the pipeline if the
                                                                // update is set to true in the configuration file
{
  auto & realRobot = ctl.realRobot(robot_);
  update(realRobot);
}

void MCKineticsObserver::update(mc_rbdyn::Robot & robot)
{
  robot.posW(X_0_fb_);
  robot.velW(v_fb_0_.vector());
}

void MCKineticsObserver::inputAdditionalWrench(const mc_rbdyn::Robot & inputRobot, const mc_rbdyn::Robot & measRobot)
{
  additionalUserResultingForce_.setZero();
  additionalUserResultingMoment_.setZero();

  for(auto wrenchSensor : measRobot.forceSensors()) // if a force sensor is not associated to a contact, its
                                                    // measurement is given as an input external wrench
  {
    if(isExternalWrench_.at(wrenchSensor.name()) == true)
    {
      additionalUserResultingForce_ += wrenchSensor.worldWrenchWithoutGravity(inputRobot).force();
      additionalUserResultingMoment_ += wrenchSensor.worldWrenchWithoutGravity(inputRobot).moment();
    }
    else
    {
      isExternalWrench_.at(wrenchSensor.name()) = true;
    }
  }
  observer_.setAdditionalWrench(additionalUserResultingForce_, additionalUserResultingMoment_);
}

void MCKineticsObserver::updateIMUs(const mc_rbdyn::Robot & measRobot, const mc_rbdyn::Robot & inputRobot)
{
  unsigned i = 0;
  for(const auto & imu : IMUs_)
  {
    mapIMUs_.insertPair(imu.name());
    /** Position of accelerometer **/

    const sva::PTransformd & bodyImuPose = inputRobot.bodySensor(imu.name()).X_b_s();
    so::kine::Kinematics bodyImuKine;
    bodyImuKine.setZero(so::kine::Kinematics::Flags::all);
    bodyImuKine.position = bodyImuPose.translation();
    bodyImuKine.orientation = so::Matrix3(bodyImuPose.rotation().transpose());

    so::kine::Kinematics worldBodyKine;

    worldBodyKine.position = inputRobot.mbc().bodyPosW[inputRobot.bodyIndexByName(imu.parentBody())].translation();
    worldBodyKine.orientation =
        so::Matrix3(inputRobot.mbc().bodyPosW[inputRobot.bodyIndexByName(imu.parentBody())].rotation().transpose());
    worldBodyKine.linVel = inputRobot.mbc().bodyVelW[inputRobot.bodyIndexByName(imu.parentBody())].linear();
    worldBodyKine.angVel = inputRobot.mbc().bodyVelW[inputRobot.bodyIndexByName(imu.parentBody())].angular();
    worldBodyKine.linAcc = worldBodyKine.orientation.toMatrix3()
                           * inputRobot.mbc().bodyAccB[inputRobot.bodyIndexByName(imu.parentBody())].linear();
    worldBodyKine.angAcc = worldBodyKine.orientation.toMatrix3()
                           * inputRobot.mbc().bodyAccB[inputRobot.bodyIndexByName(imu.parentBody())].angular();

    so::kine::Kinematics worldImuKine = worldBodyKine * bodyImuKine;
    const so::kine::Kinematics fbImuKine = worldImuKine;
    observer_.setIMU(measRobot.bodySensor().linearAcceleration(), measRobot.bodySensor().angularVelocity(),
                     acceleroSensorCovariance_, gyroSensorCovariance_, fbImuKine, mapIMUs_.getNumFromName(imu.name()));

    ++i;
  }
}

std::set<std::string> MCKineticsObserver::findContacts(const mc_control::MCController & ctl)
{
  const auto & measRobot = ctl.robot(robot_);
  auto & inputRobot = my_robots_->robot("inputRobot");

  contactsFound_.clear();

  if(withContactsDetection_)
  {
    for(const auto & contact : ctl.solver().contacts())
    {
      // std::cout << std::endl << contact.toStr() << std::endl;
      if(ctl.robots().robot(contact.r1Index()).name() == measRobot.name())
      {
        if(ctl.robots().robot(contact.r2Index()).mb().joint(0).type() == rbd::Joint::Fixed)
        {
          const auto & ifs = measRobot.indirectSurfaceForceSensor(contact.r1Surface()->name());

          if(ifs.wrenchWithoutGravity(measRobot).force().z() > measRobot.mass() * so::cst::gravityConstant * 0.05)
          {
            contactsFound_.insert(ifs.name());
            isExternalWrench_.at(ifs.name()) =
                false; // the measurement of the sensor is not passed as an input external wrench
          }
        }
      }
      else if(ctl.robots().robot(contact.r2Index()).name() == measRobot.name())
      {
        if(ctl.robots().robot(contact.r1Index()).mb().joint(0).type() == rbd::Joint::Fixed)
        {
          const auto & ifs = measRobot.indirectSurfaceForceSensor(contact.r2Surface()->name());
          if(ifs.wrenchWithoutGravity(measRobot).force().z() > measRobot.mass() * so::cst::gravityConstant * 0.05)
          {
            contactsFound_.insert(ifs.name());
            isExternalWrench_.at(ifs.name()) =
                false; // the measurement of the sensor is not passed as an input external wrench
          }
        }
      }
    }
  }
  else
  {
    for(const auto & forceSensor : measRobot.forceSensors())
    {
      if(forceSensor.wrenchWithoutGravity(measRobot).force().z() > measRobot.mass() * so::cst::gravityConstant * 0.3)
      {
        contactsFound_.insert(forceSensor.name());
        isExternalWrench_.at(forceSensor.name()) =
            false; // the measurement of the sensor is not passed as an input external wrench
      }
    }
  }

  inputAdditionalWrench(inputRobot, measRobot);
  if(!contactsFound_.empty() && !simStarted_) // we start the observation once a contact has been detected.
  {
    simStarted_ = true;
    initObserverStateVector(measRobot);
  }

  return contactsFound_;
}

void MCKineticsObserver::updateContact(const mc_rbdyn::Robot & robot,
                                       const mc_rbdyn::Robot & inputRobot,
                                       const bool & alreadySet,
                                       const mc_rbdyn::ForceSensor forceSensor,
                                       mc_rtc::Logger & logger)
{
  contactWrenchVector_.segment<3>(0) = forceSensor.wrenchWithoutGravity(robot).force(); // retrieving the force
  contactWrenchVector_.segment<3>(3) = forceSensor.wrenchWithoutGravity(robot).moment(); // retrieving the torque

  const sva::PTransformd & bodyContactPoseRobot = forceSensor.X_p_f();
  so::kine::Kinematics bodyContactKine;
  bodyContactKine.setZero(so::kine::Kinematics::Flags::all);
  bodyContactKine.position = bodyContactPoseRobot.translation();
  bodyContactKine.orientation = so::Matrix3(bodyContactPoseRobot.rotation().transpose());
  std::cout << std::endl << "bodyContactKine : " << std::endl << bodyContactKine << std::endl;

  so::kine::Kinematics worldBodyKineInputRobot;

  worldBodyKineInputRobot.position =
      inputRobot.mbc().bodyPosW[inputRobot.bodyIndexByName(forceSensor.parentBody())].translation();
  worldBodyKineInputRobot.orientation = so::Matrix3(
      inputRobot.mbc().bodyPosW[inputRobot.bodyIndexByName(forceSensor.parentBody())].rotation().transpose());
  worldBodyKineInputRobot.linVel =
      inputRobot.mbc().bodyVelW[inputRobot.bodyIndexByName(forceSensor.parentBody())].linear();
  worldBodyKineInputRobot.angVel =
      inputRobot.mbc().bodyVelW[inputRobot.bodyIndexByName(forceSensor.parentBody())].angular();
  worldBodyKineInputRobot.linAcc =
      worldBodyKineInputRobot.orientation.toMatrix3()
      * inputRobot.mbc().bodyAccB[inputRobot.bodyIndexByName(forceSensor.parentBody())].linear();
  worldBodyKineInputRobot.angAcc =
      worldBodyKineInputRobot.orientation.toMatrix3()
      * inputRobot.mbc().bodyAccB[inputRobot.bodyIndexByName(forceSensor.parentBody())].angular();

  so::kine::Kinematics worldContactKineInputRobot = worldBodyKineInputRobot * bodyContactKine;
  const so::kine::Kinematics & fbContactKineInputRobot = worldContactKineInputRobot;

  sva::PTransformd contactPos_ = forceSensor.X_p_f();
  sva::PTransformd X_0_p = inputRobot.bodyPosW(forceSensor.parentBody());
  sva::PTransformd contactPosW = contactPos_ * X_0_p;

  std::cout << std::endl << "contactPosW : " << std::endl << contactPosW.translation() << std::endl;
  std::cout << std::endl
            << "worldContactKineInputRobot pos: " << std::endl
            << worldContactKineInputRobot.position() << std::endl;

  if(alreadySet) // checks if the contact already exists, if yes, it is updated
  {
    observer_.updateContactWithWrenchSensor(contactWrenchVector_, contactSensorCovariance_, fbContactKineInputRobot,
                                            mapContacts_.getNumFromName(forceSensor.name()));
  }
  else // the contact still doesn't exist, it is added to the observer
  {
    so::kine::Kinematics
        worldContactKineRef; // reference of the contact in the control world frame using the control robot

    const int & numContact = mapContacts_.getNumFromName(forceSensor.name());
    /* robot for the kinematics in the world frame */

    if(withOdometry_)
    {
      const so::Vector3 & contactForceMeas = contactWrenchVector_.segment<3>(0);
      const so::Vector3 & contactTorqueMeas = contactWrenchVector_.segment<3>(3);
      const so::kine::Kinematics worldContactKine = observer_.getGlobalKinematicsOf(fbContactKineInputRobot);
      worldContactKineRef.position = (worldContactKine.orientation.toMatrix3() * contactForceMeas
                                      + flexStiffness_.linear().asDiagonal() * worldContactKine.position()
                                      + flexStiffness_.angular().asDiagonal() * worldContactKine.linVel())
                                         .cwiseQuotient(flexStiffness_.linear());

      so::Vector3 vecS = (2 * worldContactKine.orientation.toMatrix3().transpose()
                          * (contactTorqueMeas
                             + worldContactKine.orientation.toMatrix3() * flexDamping_.angular().asDiagonal()
                                   * worldContactKine.angVel()))
                             .cwiseQuotient(flexStiffness_.angular());

      worldContactKineRef.orientation =
          so::Matrix3(so::kine::skewSymmetric(vecS) * worldContactKine.orientation.toMatrix3());
      std::cout << std::endl << "worldContactKineRef : " << std::endl << worldContactKineRef << std::endl;
    }
    else
    {
      so::kine::Kinematics worldBodyKineRobot;
      worldBodyKineRobot.position = robot.mbc().bodyPosW[robot.bodyIndexByName(forceSensor.parentBody())].translation();
      worldBodyKineRobot.orientation =
          so::Matrix3(robot.mbc().bodyPosW[robot.bodyIndexByName(forceSensor.parentBody())].rotation().transpose());

      std::cout << std::endl << "forceSensor.name() : " << std::endl << forceSensor.name() << std::endl;
      std::cout << std::endl << "forceSensor.parentBody() : " << std::endl << forceSensor.parentBody() << std::endl;
      std::cout << std::endl << "worldBodyKineRobot : " << std::endl << worldBodyKineRobot << std::endl;
      worldContactKineRef = worldBodyKineRobot * bodyContactKine;
    }

    if(observer_.getNumberOfContacts() > 0) // checks if another contact is already set
    {
      observer_.addContact(worldContactKineRef, contactInitCovarianceNewContacts_, contactProcessCovariance_,
                           numContact, flexStiffness_.linear().asDiagonal(), flexDamping_.linear().asDiagonal(),
                           flexStiffness_.angular().asDiagonal(), flexDamping_.angular().asDiagonal());
    }
    else
    {
      observer_.addContact(worldContactKineRef, contactInitCovarianceFirstContacts_, contactProcessCovariance_,
                           numContact, flexStiffness_.linear().asDiagonal(), flexDamping_.linear().asDiagonal(),
                           flexStiffness_.angular().asDiagonal(), flexDamping_.angular().asDiagonal());
    }
    observer_.updateContactWithWrenchSensor(contactWrenchVector_,
                                            contactSensorCovariance_, // contactInitCovariance_.block<6,6>(6,6)
                                            fbContactKineInputRobot, numContact);
    addContactLogEntries(logger, numContact);
  }
}

void MCKineticsObserver::updateContacts(const mc_rbdyn::Robot & robot,
                                        const mc_rbdyn::Robot & inputRobot,
                                        std::set<std::string> updatedContacts,
                                        mc_rtc::Logger & logger)
{
  /** Debugging output **/
  if(verbose_ && updatedContacts != oldContacts_)
    mc_rtc::log::info("[{}] Contacts changed: {}", name(), mc_rtc::io::to_string(updatedContacts));
  contactPositions_.clear();

  bool contactAlreadySet;
  for(const auto & updatedContact : updatedContacts)
  {
    contactAlreadySet = false;
    if(oldContacts_.find(updatedContact)
       != oldContacts_.end()) // checks if the contact already exists, if yes, it is updated
    {
      contactAlreadySet = true;
    }
    else
    {
      mapContacts_.insertPair(robot.forceSensor(updatedContact).name());
    }

    updateContact(robot, inputRobot, contactAlreadySet, robot.forceSensor(updatedContact), logger);
  }
  std::set<std::string>
      diffs; // List of the contact that were available on last iteration but are not set anymore on the current one
  std::set_difference(oldContacts_.begin(), oldContacts_.end(), updatedContacts.begin(), updatedContacts.end(),
                      std::inserter(diffs, diffs.end()));
  for(const auto & diff : diffs)
  {
    int numDiff = mapContacts_.getNumFromName(diff);
    observer_.removeContact(numDiff);
    removeContactLogEntries(logger, numDiff);
  }

  oldContacts_ = updatedContacts;
  unsigned nbContacts = static_cast<unsigned>(oldContacts_.size());
  if(debug_)
  {
    mc_rtc::log::info("nbContacts = {}", nbContacts);
  }
}

void MCKineticsObserver::mass(double mass)
{
  mass_ = mass;
  observer_.setMass(mass);
}

void MCKineticsObserver::flexStiffness(const sva::MotionVecd & stiffness)
{
  flexStiffness_ = stiffness;
}

void MCKineticsObserver::flexDamping(const sva::MotionVecd & damping)
{
  flexDamping_ = damping;
}

///////////////////////////////////////////////////////////////////////
/// -------------------------------Logs--------------------------------
///////////////////////////////////////////////////////////////////////

void MCKineticsObserver::addToLogger(const mc_control::MCController &,
                                     mc_rtc::Logger & logger,
                                     const std::string & category)
{
  logger.addLogEntry(category + "_mcko_fb_posW", [this]() -> const sva::PTransformd & { return X_0_fb_; });
  logger.addLogEntry(category + "_mcko_fb_velW", [this]() -> const sva::MotionVecd & { return v_fb_0_; });
  logger.addLogEntry(category + "_mcko_fb_accW", [this]() -> const sva::MotionVecd & { return a_fb_0_; });

  logger.addLogEntry(category + "_constants_mass", [this]() -> const double & { return observer_.getMass(); });
  logger.addLogEntry(category + "_constants_flexStiffness",
                     [this]() -> const sva::MotionVecd & { return flexStiffness_; });
  logger.addLogEntry(category + "_constants_flexDamping", [this]() -> const sva::MotionVecd & { return flexDamping_; });
}

void MCKineticsObserver::removeFromLogger(mc_rtc::Logger & logger, const std::string & category)
{
  logger.removeLogEntry(category + "_posW");
  logger.removeLogEntry(category + "_velW");
  logger.removeLogEntry(category + "_mass");
  logger.removeLogEntry(category + "_flexStiffness");
  logger.removeLogEntry(category + "_flexDamping");
}

void MCKineticsObserver::addToGUI(const mc_control::MCController &,
                                  mc_rtc::gui::StateBuilder & gui,
                                  const std::vector<std::string> & category)
{
  using namespace mc_rtc::gui;
  // clang-format off
  gui.addElement(category,
    mc_state_observation::gui::make_input_element("Accel Covariance", acceleroSensorCovariance_(0,0)),
    mc_state_observation::gui::make_input_element("Force Covariance", contactSensorCovariance_(0,0)),
    mc_state_observation::gui::make_input_element("Gyro Covariance", gyroSensorCovariance_(0,0)),
    mc_state_observation::gui::make_input_element("Flex Stiffness", flexStiffness_), 
    mc_state_observation::gui::make_input_element("Flex Damping", flexDamping_),
    Label("contacts", [this]() { return mc_rtc::io::to_string(oldContacts_); }));
  // clang-format on
}

void MCKineticsObserver::plotVariablesBeforeUpdate(mc_rtc::Logger & logger)
{
  /* Plots of the updated state */
  logger.addLogEntry(category_ + "_globalWorldCentroidState_positionW_",
                     [this]() -> Eigen::Vector3d { return globalCentroidKinematics_.position(); });
  logger.addLogEntry(category_ + "_globalWorldCentroidState_linVelW",
                     [this]() -> Eigen::Vector3d { return globalCentroidKinematics_.linVel(); });
  logger.addLogEntry(category_ + "_globalWorldCentroidState_linAccW",
                     [this]() -> Eigen::Vector3d { return globalCentroidKinematics_.linAcc(); });
  logger.addLogEntry(category_ + "_globalWorldCentroidState_oriW",
                     [this]() -> Eigen::Quaternion<double>
                     { return globalCentroidKinematics_.orientation.inverse().toQuaternion(); });
  logger.addLogEntry(category_ + "_globalWorldCentroidState_angVelW",
                     [this]() -> Eigen::Vector3d { return globalCentroidKinematics_.angVel(); });
  logger.addLogEntry(category_ + "_globalWorldCentroidState_angAccW",
                     [this]() -> Eigen::Vector3d { return globalCentroidKinematics_.angAcc(); });
  for(const auto & imu : IMUs_)
  {
    logger.addLogEntry(category_ + "_globalWorldCentroidState_gyroBias_" + imu.name(),
                       [this, imu]() -> Eigen::Vector3d
                       {
                         return observer_.getCurrentStateVector().segment<int(observer_.sizeGyroBias)>(
                             observer_.gyroBiasIndex(mapIMUs_.getNumFromName(imu.name())));
                       });
  }
  logger.addLogEntry(
      category_ + "_globalWorldCentroidState_extForceCentr",
      [this]() -> Eigen::Vector3d
      { return observer_.getCurrentStateVector().segment<int(observer_.sizeForce)>(observer_.unmodeledForceIndex()); });

  logger.addLogEntry(category_ + "_globalWorldCentroidState_extTorqueCentr",
                     [this]() -> Eigen::Vector3d {
                       return observer_.getCurrentStateVector().segment<int(observer_.sizeTorque)>(
                           observer_.unmodeledTorqueIndex());
                     });

  /* Inputs */
  logger.addLogEntry(category_ + "_inputs_additionalWrench_Force",
                     [this]() -> Eigen::Vector3d
                     { return observer_.getAdditionalWrench().segment<int(observer_.sizeForce)>(0); });
  logger.addLogEntry(category_ + "_inputs_additionalWrench_Torque",
                     [this]() -> Eigen::Vector3d {
                       return observer_.getAdditionalWrench().segment<int(observer_.sizeTorque)>(observer_.sizeForce);
                     });

  /* State covariances */
  logger.addLogEntry(category_ + "_stateCovariances_positionW_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF()
                           .getStateCovariance()
                           .block<int(observer_.sizePosTangent), int(observer_.sizePosTangent)>(
                               observer_.posIndexTangent(), observer_.posIndexTangent())
                           .diagonal();
                     });
  logger.addLogEntry(category_ + "_stateCovariances_orientationW_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF()
                           .getStateCovariance()
                           .block<int(observer_.sizeOriTangent), int(observer_.sizeOriTangent)>(
                               observer_.oriIndexTangent(), observer_.oriIndexTangent())
                           .diagonal();
                     });
  logger.addLogEntry(category_ + "_stateCovariances_linVelW_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF()
                           .getStateCovariance()
                           .block<int(observer_.sizeLinVelTangent), int(observer_.sizeLinVelTangent)>(
                               observer_.linVelIndexTangent(), observer_.linVelIndexTangent())
                           .diagonal();
                     });
  logger.addLogEntry(category_ + "_stateCovariances_angVelW_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF()
                           .getStateCovariance()
                           .block<int(observer_.sizeAngVelTangent), int(observer_.sizeAngVelTangent)>(
                               observer_.angVelIndexTangent(), observer_.angVelIndexTangent())
                           .diagonal();
                     });

  for(const auto & imu : IMUs_)
  {
    logger.addLogEntry(category_ + "_stateCovariances_gyroBias_" + imu.name(),
                       [this, imu]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF()
                             .getStateCovariance()
                             .block<int(observer_.sizeGyroBiasTangent), int(observer_.sizeGyroBiasTangent)>(
                                 observer_.gyroBiasIndexTangent(mapIMUs_.getNumFromName(imu.name())),
                                 observer_.gyroBiasIndexTangent(mapIMUs_.getNumFromName(imu.name())))
                             .diagonal();
                       });
  }

  logger.addLogEntry(category_ + "_stateCovariances_extForce_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF()
                           .getStateCovariance()
                           .block<int(observer_.sizeForceTangent), int(observer_.sizeForceTangent)>(
                               observer_.unmodeledForceIndexTangent(), observer_.unmodeledForceIndexTangent())
                           .diagonal();
                     });
  logger.addLogEntry(category_ + "_stateCovariances_extTorque_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF()
                           .getStateCovariance()
                           .block<int(observer_.sizeTorqueTangent), int(observer_.sizeTorqueTangent)>(
                               observer_.unmodeledTorqueIndexTangent(), observer_.unmodeledTorqueIndexTangent())
                           .diagonal();
                     });
}

void MCKineticsObserver::plotVariablesAfterUpdate(mc_rtc::Logger & logger)
{
  /* Plots of the predicted state */
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_worldImu_Ori_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> so::Quaternion { return robotImuOri_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_worldImu_Ori_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> so::Quaternion { return robotImuOri_1; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_worldImu_Ori_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> so::Quaternion { return realRobotImuOri_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_worldImu_Ori_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> so::Quaternion { return realRobotImuOri_1; });
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_worldFb_Ori_", [this]() -> so::Quaternion { return robotFbOri_; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_worldFb_Ori_",
                     [this]() -> so::Quaternion { return realRobotFbOri_; });
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_ImuInFb_Pos_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> Eigen::Vector3d { return robotPosImuInFB_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_ImuInFb_Pos_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> Eigen::Vector3d { return robotPosImuInFB_1; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_ImuInFb_Pos_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> Eigen::Vector3d { return realRobotPosImuInFB_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_ImuInFb_Pos_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> Eigen::Vector3d { return realRobotPosImuInFB_1; });
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_Tilt_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> Eigen::Vector3d { return robotTilt_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_robot_Tilt_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> Eigen::Vector3d { return robotTilt_1; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_Tilt_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> Eigen::Vector3d { return realRobotTilt_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_realRobot_Tilt_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> Eigen::Vector3d { return realRobotTilt_1; });
  logger.addLogEntry(category_ + "_robotsFbIMU_MCKO_Tilt_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> Eigen::Vector3d { return MCKOrobotTilt_0; });
  logger.addLogEntry(category_ + "_robotsFbIMU_MCKO_Tilt_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> Eigen::Vector3d { return MCKOrobotTilt_1; });
  logger.addLogEntry(category_ + "_realRobot_centroidImuOri_" + mapIMUs_.getNameFromNum(0),
                     [this]() -> so::Quaternion { return realRobot_centroidImuOri_0; });
  logger.addLogEntry(category_ + "_realRobot_centroidImuOri_" + mapIMUs_.getNameFromNum(1),
                     [this]() -> so::Quaternion { return realRobot_centroidImuOri_1; });

  logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_positionW_",
                     [this]() -> Eigen::Vector3d
                     { return predictedGlobalCentroidState_.segment<int(observer_.sizePos)>(observer_.posIndex()); });
  logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_linVelW",
                     [this]() -> Eigen::Vector3d {
                       return predictedGlobalCentroidState_.segment<int(observer_.sizeLinVel)>(observer_.linVelIndex());
                     });

  logger.addLogEntry(
      category_ + "_predictedGlobalCentroidKinematics_oriW",
      [this]() -> Eigen::Vector3d
      {
        so::kine::Orientation ori;
        return ori.fromVector4(predictedGlobalCentroidState_.segment<int(observer_.sizeOri)>(observer_.oriIndex()))
            .toRotationVector();
      });
  logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_angVelW",
                     [this]() -> Eigen::Vector3d {
                       return predictedGlobalCentroidState_.segment<int(observer_.sizeAngVel)>(observer_.angVelIndex());
                     });

  for(const auto & imu : IMUs_)
  {
    logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_gyroBias" + imu.name() + "_measured",
                       [this, imu]() -> Eigen::Vector3d
                       {
                         return predictedGlobalCentroidState_.segment<int(observer_.sizeGyroBias)>(
                             observer_.gyroBiasIndexTangent(mapIMUs_.getNumFromName(imu.name())));
                       });
  }

  /* Plots of the inputs */

  logger.addLogEntry(category_ + "_inputs_angularMomentum",
                     [this]() -> Eigen::Vector3d { return observer_.getAngularMomentum()(); });
  logger.addLogEntry(category_ + "_inputs_angularMomentumDot",
                     [this]() -> Eigen::Vector3d { return observer_.getAngularMomentumDot()(); });
  logger.addLogEntry(category_ + "_inputs_com", [this]() -> Eigen::Vector3d { return observer_.getCenterOfMass()(); });
  logger.addLogEntry(category_ + "_inputs_comDot",
                     [this]() -> Eigen::Vector3d { return observer_.getCenterOfMassDot()(); });
  logger.addLogEntry(category_ + "_inputs_comDotDot",
                     [this]() -> Eigen::Vector3d { return observer_.getCenterOfMassDotDot()(); });
  logger.addLogEntry(category_ + "_inputs_inertiaMatrix",
                     [this]() -> Eigen::Vector6d
                     {
                       so::Vector6 inertia;
                       inertia.segment<3>(0) = observer_.getInertiaMatrix()().diagonal();
                       inertia.segment<2>(3) = observer_.getInertiaMatrix()().block<1, 2>(0, 1);
                       inertia(5) = observer_.getInertiaMatrix()()(1, 2);
                       return inertia;
                     });

  logger.addLogEntry(category_ + "_inputs_inertiaMatrixDot",
                     [this]() -> Eigen::Vector6d
                     {
                       so::Vector6 inertiaDot;
                       inertiaDot.segment<3>(0) = observer_.getInertiaMatrixDot()().diagonal();
                       inertiaDot.segment<2>(3) = observer_.getInertiaMatrixDot()().block<1, 2>(0, 1);
                       inertiaDot(5) = observer_.getInertiaMatrixDot()()(1, 2);
                       return inertiaDot;
                     });

  /* Plots of the measurements */
  {
    for(const auto & imu : IMUs_)
    {
      logger.addLogEntry(category_ + "_measurements_gyro_" + imu.name() + "_measured",
                         [this, imu]() -> Eigen::Vector3d
                         {
                           return observer_.getEKF().getLastMeasurement().segment<int(observer_.sizeGyroBias)>(
                               observer_.getIMUMeasIndexByNum(mapIMUs_.getNumFromName(imu.name()))
                               + observer_.sizeAcceleroSignal);
                         });
      logger.addLogEntry(category_ + "_measurements_gyro_" + imu.name() + "_predicted",
                         [this, imu]() -> Eigen::Vector3d
                         {
                           return observer_.getEKF().getLastPredictedMeasurement().segment<int(observer_.sizeGyroBias)>(
                               observer_.getIMUMeasIndexByNum(mapIMUs_.getNumFromName(imu.name()))
                               + observer_.sizeAcceleroSignal);
                         });
      logger.addLogEntry(category_ + "_measurements_gyro_" + imu.name() + "_corrected",
                         [this, imu]() -> Eigen::Vector3d
                         {
                           return correctedMeasurements_.segment<int(observer_.sizeGyroBias)>(
                               observer_.getIMUMeasIndexByNum(mapIMUs_.getNumFromName(imu.name()))
                               + observer_.sizeAcceleroSignal);
                         });

      logger.addLogEntry(category_ + "_measurements_accelerometer_" + imu.name() + "_measured",
                         [this, imu]() -> Eigen::Vector3d
                         {
                           return observer_.getEKF().getLastMeasurement().segment<int(observer_.sizeAcceleroSignal)>(
                               observer_.getIMUMeasIndexByNum(mapIMUs_.getNumFromName(imu.name())));
                         });
      logger.addLogEntry(
          category_ + "_measurements_accelerometer_" + imu.name() + "_predicted",
          [this, imu]() -> Eigen::Vector3d
          {
            return observer_.getEKF().getLastPredictedMeasurement().segment<int(observer_.sizeAcceleroSignal)>(
                observer_.getIMUMeasIndexByNum(mapIMUs_.getNumFromName(imu.name())));
          });
      logger.addLogEntry(category_ + "_measurements_accelerometer_" + imu.name() + "_corrected",
                         [this, imu]() -> Eigen::Vector3d
                         {
                           return correctedMeasurements_.segment<int(observer_.sizeAcceleroSignal)>(
                               observer_.getIMUMeasIndexByNum(mapIMUs_.getNumFromName(imu.name())));
                         });

      logger.addLogEntry(category_ + "_debug_IMU_Tilt" + imu.name(),
                         [this, imu]() -> Eigen::Vector3d
                         { return predictedAccelerometersGravityComponent_.at(mapIMUs_.getNumFromName(imu.name())); });

      logger.addLogEntry(category_ + "_debug_IMU_linearIMUAcc" + imu.name(),
                         [this, imu]() -> Eigen::Vector3d
                         { return predictedWorldIMUsLinAcc_.at(mapIMUs_.getNumFromName(imu.name())); });
    }
  }

  /* Plots of the innovation */
  logger.addLogEntry(category_ + "_innovation_positionW_",
                     [this]() -> Eigen::Vector3d {
                       return observer_.getEKF().getInnovation().segment<int(observer_.sizePosTangent)>(
                           observer_.posIndexTangent());
                     });
  logger.addLogEntry(category_ + "_innovation_linVelW_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF().getInnovation().segment<int(observer_.sizeLinVelTangent)>(
                           observer_.linVelIndexTangent());
                     });
  logger.addLogEntry(category_ + "_innovation_oriW_",
                     [this]() -> Eigen::Vector3d {
                       return observer_.getEKF().getInnovation().segment<int(observer_.sizeOriTangent)>(
                           observer_.oriIndexTangent());
                     });
  logger.addLogEntry(category_ + "_innovation_angVelW_",
                     [this]() -> Eigen::Vector3d
                     {
                       return observer_.getEKF().getInnovation().segment<int(observer_.sizeAngVelTangent)>(
                           observer_.angVelIndexTangent());
                     });
  logger.addLogEntry(category_ + "_debug_worldInputRobotKine_position",
                     [this]() -> Eigen::Vector3d { return my_robots_->robot("inputRobot").posW().translation(); });
  logger.addLogEntry(category_ + "_debug_worldInputRobotKine_orientation",
                     [this]() -> Eigen::Quaternion<double>
                     {
                       return so::kine::Orientation(so::Matrix3(my_robots_->robot("inputRobot").posW().rotation()))
                           .inverse()
                           .toQuaternion();
                     });
  logger.addLogEntry(category_ + "_debug_worldInputRobotKine_linVel",
                     [this]() -> Eigen::Vector3d { return my_robots_->robot("inputRobot").velW().linear(); });
  logger.addLogEntry(category_ + "_debug_worldInputRobotKine_angVel",
                     [this]() -> Eigen::Vector3d { return my_robots_->robot("inputRobot").velW().angular(); });
  logger.addLogEntry(category_ + "_debug_worldInputRobotKine_linAcc",
                     [this]() -> Eigen::Vector3d { return my_robots_->robot("inputRobot").accW().linear(); });
  logger.addLogEntry(category_ + "_debug_worldInputRobotKine_angAcc",
                     [this]() -> Eigen::Vector3d { return my_robots_->robot("inputRobot").accW().angular(); });
}

void MCKineticsObserver::addContactLogEntries(mc_rtc::Logger & logger, const int & numContact)
{
  if(observer_.getContactIsSetByNum(numContact))
  {
    logger.addLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                           + "_position",
                       [this, numContact]() -> Eigen::Vector3d {
                         return observer_.getCurrentStateVector().segment<int(observer_.sizePos)>(
                             observer_.contactPosIndex(numContact));
                       });
    logger.addLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                           + "_orientation",
                       [this, numContact]() -> Eigen::Quaternion<double>
                       {
                         so::kine::Orientation ori;
                         return ori
                             .fromVector4(observer_.getCurrentStateVector().segment<int(observer_.sizeOri)>(
                                 observer_.contactOriIndex(numContact)))
                             .inverse()
                             .toQuaternion();
                       });
    logger.addLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                           + "_forces",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getCurrentStateVector().segment<int(observer_.sizeForce)>(
                             observer_.contactForceIndex(numContact));
                       });
    logger.addLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                           + "_torques",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return globalCentroidKinematics_.orientation.toMatrix3()
                                * observer_.getCurrentStateVector().segment<int(observer_.sizeTorque)>(
                                    observer_.contactTorqueIndex(numContact));
                       });
    logger.addLogEntry(
        category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact) + "_position_",
        [this, numContact]() -> Eigen::Vector3d
        {
          return observer_.getEKF()
              .getStateCovariance()
              .block<int(observer_.sizePosTangent), int(observer_.sizePosTangent)>(
                  observer_.contactPosIndexTangent(numContact), observer_.contactPosIndexTangent(numContact))
              .diagonal();
        });
    logger.addLogEntry(
        category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact) + "_orientation_",
        [this, numContact]() -> Eigen::Vector3d
        {
          return observer_.getEKF()
              .getStateCovariance()
              .block<int(observer_.sizeOriTangent), int(observer_.sizeOriTangent)>(
                  observer_.contactOriIndexTangent(numContact), observer_.contactOriIndexTangent(numContact))
              .diagonal();
        });
    logger.addLogEntry(category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact) + "_Force_",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF()
                             .getStateCovariance()
                             .block<int(observer_.sizeForceTangent), int(observer_.sizeForceTangent)>(
                                 observer_.contactForceIndexTangent(numContact),
                                 observer_.contactForceIndexTangent(numContact))
                             .diagonal();
                       });
    logger.addLogEntry(category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact) + "_Torque_",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF()
                             .getStateCovariance()
                             .block<int(observer_.sizeTorqueTangent), int(observer_.sizeTorqueTangent)>(
                                 observer_.contactTorqueIndexTangent(numContact),
                                 observer_.contactTorqueIndexTangent(numContact))
                             .diagonal();
                       });

    logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                           + mapContacts_.getNameFromNum(numContact) + "_position",
                       [this, numContact]() -> Eigen::Vector3d {
                         return predictedGlobalCentroidState_.segment<int(observer_.sizePos)>(
                             observer_.contactPosIndex(numContact));
                       });
    logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                           + mapContacts_.getNameFromNum(numContact) + "_orientation",
                       [this, numContact]() -> Eigen::Quaternion<double>
                       {
                         so::kine::Orientation ori;
                         return ori
                             .fromVector4(predictedGlobalCentroidState_.segment<int(observer_.sizeOri)>(
                                 observer_.contactOriIndex(numContact)))
                             .inverse()
                             .toQuaternion();
                       });
    logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                           + mapContacts_.getNameFromNum(numContact) + "_forces",
                       [this, numContact]() -> Eigen::Vector3d {
                         return predictedGlobalCentroidState_.segment<int(observer_.sizeForce)>(
                             observer_.contactForceIndex(numContact));
                       });
    logger.addLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                           + mapContacts_.getNameFromNum(numContact) + "_torques",
                       [this, numContact]() -> Eigen::Vector3d {
                         return predictedGlobalCentroidState_.segment<int(observer_.sizeTorque)>(
                             observer_.contactTorqueIndex(numContact));
                       });

    logger.addLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_position",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getInnovation().segment<int(observer_.sizePos)>(
                             observer_.contactPosIndexTangent(numContact));
                       });
    logger.addLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_orientation",
                       [this, numContact]() -> Eigen::Quaternion<double>
                       {
                         so::kine::Orientation ori;
                         return ori
                             .fromVector4(observer_.getEKF().getInnovation().segment<int(observer_.sizeOri)>(
                                 observer_.contactOriIndexTangent(numContact)))
                             .inverse()
                             .toQuaternion();
                       });
    logger.addLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_forces",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getInnovation().segment<int(observer_.sizeForce)>(
                             observer_.contactForceIndexTangent(numContact));
                       });
    logger.addLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_torques",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getInnovation().segment<int(observer_.sizeTorque)>(
                             observer_.contactTorqueIndexTangent(numContact));
                       });

    logger.addLogEntry(category_ + "_measurements_contacts_force_" + mapContacts_.getNameFromNum(numContact)
                           + "_measured",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getLastMeasurement().segment<int(observer_.sizeForce)>(
                             observer_.getContactMeasIndexByNum(numContact));
                       });
    logger.addLogEntry(category_ + "_measurements_contacts_force_" + mapContacts_.getNameFromNum(numContact)
                           + "_predicted",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getLastPredictedMeasurement().segment<int(observer_.sizeForce)>(
                             observer_.getContactMeasIndexByNum(numContact));
                       });
    logger.addLogEntry(category_ + "_measurements_contacts_force_" + mapContacts_.getNameFromNum(numContact)
                           + "_corrected",
                       [this, numContact]() -> Eigen::Vector3d {
                         return correctedMeasurements_.segment<int(observer_.sizeForce)>(
                             observer_.getContactMeasIndexByNum(numContact));
                       });

    logger.addLogEntry(category_ + "_measurements_contacts_torque_" + mapContacts_.getNameFromNum(numContact)
                           + "_measured",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getLastMeasurement().segment<int(observer_.sizeTorque)>(
                             observer_.getContactMeasIndexByNum(numContact) + observer_.sizeForce);
                       });
    logger.addLogEntry(category_ + "_measurements_contacts_torque_" + mapContacts_.getNameFromNum(numContact)
                           + "_predicted",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return observer_.getEKF().getLastPredictedMeasurement().segment<int(observer_.sizeTorque)>(
                             observer_.getContactMeasIndexByNum(numContact) + observer_.sizeForce);
                       });
    logger.addLogEntry(category_ + "_measurements_contacts_torque_" + mapContacts_.getNameFromNum(numContact)
                           + "_corrected",
                       [this, numContact]() -> Eigen::Vector3d
                       {
                         return correctedMeasurements_.segment<int(observer_.sizeTorque)>(
                             observer_.getContactMeasIndexByNum(numContact) + observer_.sizeForce);
                       });

    logger.addLogEntry(category_ + "_debug_contactWrench_World_" + mapContacts_.getNameFromNum(numContact) + "_force",
                       [this, numContact]() -> Eigen::Vector3d
                       { return observer_.getWorldContactWrench(numContact).segment<int(observer_.sizeForce)>(0); });

    logger.addLogEntry(category_ + "_debug_contactWrench_World_" + mapContacts_.getNameFromNum(numContact) + "_torque",
                       [this, numContact]() -> Eigen::Vector3d
                       { return observer_.getWorldContactWrench(numContact).segment<int(observer_.sizeTorque)>(3); });

    logger.addLogEntry(category_ + "_debug_contactWrench_Centroid_" + mapContacts_.getNameFromNum(numContact)
                           + "_force",
                       [this, numContact]() -> Eigen::Vector3d
                       { return observer_.getCentroidContactWrench(numContact).segment<int(observer_.sizeForce)>(0); });

    logger.addLogEntry(
        category_ + "_debug_contactWrench_Centroid_" + mapContacts_.getNameFromNum(numContact) + "_torque",
        [this, numContact]() -> Eigen::Vector3d
        { return observer_.getCentroidContactWrench(numContact).segment<int(observer_.sizeTorque)>(3); });

    logger.addLogEntry(category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact)
                           + "_inputWorldRef_position",
                       [this, numContact]() -> Eigen::Vector3d
                       { return observer_.getWorldContactInputRefPose(numContact).position(); });

    logger.addLogEntry(
        category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact) + "_inputWorldRef_orientation",
        [this, numContact]() -> Eigen::Quaternion<double>
        { return observer_.getWorldContactInputRefPose(numContact).orientation.inverse().toQuaternion(); });

    logger.addLogEntry(category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact)
                           + "_inputCentroidContactKine_position",
                       [this, numContact]() -> Eigen::Vector3d
                       { return observer_.getCentroidContactInputPose(numContact).position(); });

    logger.addLogEntry(category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact)
                           + "_inputCentroidContactKine_orientation",
                       [this, numContact]() -> Eigen::Quaternion<double> {
                         return observer_.getCentroidContactInputPose(numContact).orientation.inverse().toQuaternion();
                       });

    logger.addLogEntry(category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact)
                           + "_worldContactPoseFromCentroid_position",
                       [this, numContact]() -> Eigen::Vector3d
                       { return observer_.getWorldContactPose(numContact).position(); });

    logger.addLogEntry(category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact)
                           + "_worldContactPoseFromCentroid_orientation",
                       [this, numContact]() -> Eigen::Quaternion<double>
                       { return observer_.getWorldContactPose(numContact).orientation.inverse().toQuaternion(); });

    logger.addLogEntry(
        category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact) + "_inputUserContactKine_position",
        [this, numContact]() -> Eigen::Vector3d { return observer_.getUserContactInputPose(numContact).position(); });

    logger.addLogEntry(category_ + "_debug_contactPose_" + mapContacts_.getNameFromNum(numContact)
                           + "_inputUserContactKine_orientation",
                       [this, numContact]() -> Eigen::Quaternion<double>
                       { return observer_.getUserContactInputPose(numContact).orientation.inverse().toQuaternion(); });
  }
}

void MCKineticsObserver::removeContactLogEntries(mc_rtc::Logger & logger, const int & numContact)
{
  logger.removeLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_position");
  logger.removeLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_orientation");
  logger.removeLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_forces");
  logger.removeLogEntry(category_ + "_globalWorldCentroidState_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_torques");

  logger.removeLogEntry(category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_position_");
  logger.removeLogEntry(category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_orientation_");
  logger.removeLogEntry(category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact) + "_Force_");
  logger.removeLogEntry(category_ + "_stateCovariances_contact_" + mapContacts_.getNameFromNum(numContact)
                        + "_Torque_");

  logger.removeLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                        + mapContacts_.getNameFromNum(numContact) + "_position");
  logger.removeLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                        + mapContacts_.getNameFromNum(numContact) + "_orientation");
  logger.removeLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                        + mapContacts_.getNameFromNum(numContact) + "_forces");
  logger.removeLogEntry(category_ + "_predictedGlobalCentroidKinematics_contact_"
                        + mapContacts_.getNameFromNum(numContact) + "_torques");

  logger.removeLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_position");
  logger.removeLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_orientation");
  logger.removeLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_forces");
  logger.removeLogEntry(category_ + "_innovation_contact_" + mapContacts_.getNameFromNum(numContact) + "_torques");

  logger.removeLogEntry(category_ + "_measurements_contacts_force_" + mapContacts_.getNameFromNum(numContact)
                        + "_measured");
  logger.removeLogEntry(category_ + "_measurements_contacts_force_" + mapContacts_.getNameFromNum(numContact)
                        + "_predicted");
  logger.removeLogEntry(category_ + "_measurements_contacts_force_" + mapContacts_.getNameFromNum(numContact)
                        + "_corrected");
  logger.removeLogEntry(category_ + "_measurements_contacts_torque_" + mapContacts_.getNameFromNum(numContact)
                        + "_measured");
  logger.removeLogEntry(category_ + "_measurements_contacts_torque_" + mapContacts_.getNameFromNum(numContact)
                        + "_predicted");
  logger.removeLogEntry(category_ + "_measurements_contacts_torque_" + mapContacts_.getNameFromNum(numContact)
                        + "_corrected");

  logger.removeLogEntry(category_ + "_contactWrench_World_" + mapContacts_.getNameFromNum(numContact) + "_force");
  logger.removeLogEntry(category_ + "_contactWrench_World_" + mapContacts_.getNameFromNum(numContact) + "_torque");

  logger.removeLogEntry(category_ + "_contactWrench_Centroid_" + mapContacts_.getNameFromNum(numContact) + "_force");
  logger.removeLogEntry(category_ + "_contactWrench_Centroid_" + mapContacts_.getNameFromNum(numContact) + "_torque");
}

} // namespace mc_state_observation

EXPORT_OBSERVER_MODULE("MCKineticsObserver", mc_state_observation::MCKineticsObserver)