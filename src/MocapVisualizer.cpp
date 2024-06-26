/* Copyright 2017-2020 CNRS-AIST JRL, CNRS-UM LIRMM */

#include <mc_control/MCController.h>
#include <mc_observers/ObserverMacros.h>
#include <mc_rtc/io_utils.h>
#include <mc_rtc/logging.h>
#include <mc_state_observation/MocapVisualizer.h>
#include <mc_state_observation/conversions/kinematics.h>
#include <mc_state_observation/gui_helpers.h>

namespace mc_state_observation
{
MocapVisualizer::MocapVisualizer(const std::string & type, double dt) : mc_observers::Observer(type, dt) {}

///////////////////////////////////////////////////////////////////////
/// --------------------------Core functions---------------------------
///////////////////////////////////////////////////////////////////////

void MocapVisualizer::configure(const mc_control::MCController & ctl, const mc_rtc::Configuration & config)
{
  robot_ = config("robot", ctl.robot().name());
  csvPath_ = "/home/arnaud/Documents/mocap/resampledMocapData.csv";

  double contactDetectionPropThreshold = config("contactDetectionPropThreshold", 0.11);

  using ContactsManager = measurements::ContactsManager<MocapContact>;

  mocapBodyName_ = static_cast<std::string>(config("mocapBodyName"));

  std::string contactsDetectionString = static_cast<std::string>(config("contactsDetection"));
  ContactsManager::ContactsDetection contactsDetectionMethod =
      contactsManager_.stringToContactsDetection(contactsDetectionString, observerName_);

  if(contactsDetectionMethod == ContactsManager::ContactsDetection::Surfaces)
  {
    std::vector<std::string> surfacesForContactDetection =
        config("surfacesForContactDetection", std::vector<std::string>());

    measurements::ContactsManagerSurfacesConfiguration contactsConfig(observerName_, surfacesForContactDetection);

    contactsConfig.contactDetectionPropThreshold(contactDetectionPropThreshold).verbose(true);

    contactsManager_.init(ctl, robot_, contactsConfig);
  }
  if(contactsDetectionMethod == ContactsManager::ContactsDetection::Sensors)
  {
    measurements::ContactsManagerSensorsConfiguration contactsConfig(observerName_);
    contactsConfig.contactDetectionPropThreshold(contactDetectionPropThreshold).verbose(true);
    contactsManager_.init(ctl, robot_, contactsConfig);
  }
  if(contactsDetectionMethod == ContactsManager::ContactsDetection::Solver)
  {
    measurements::ContactsManagerSolverConfiguration contactsConfig(observerName_);
    contactsConfig.contactDetectionPropThreshold(contactDetectionPropThreshold).verbose(true);
    contactsManager_.init(ctl, robot_, contactsConfig);
  }
}

void MocapVisualizer::reset(const mc_control::MCController & ctl)
{
  const auto & robot = ctl.robot(robot_);
  const auto & realRobot = ctl.realRobot(robot_);

  my_robots_ = mc_rbdyn::Robots::make();
  my_robots_->robotCopy(robot, robot.name());
  ctl.gui()->addElement(
      {"Robots"},
      mc_rtc::gui::Robot("MocapVisualizer", [this]() -> const mc_rbdyn::Robot & { return my_robots_->robot(); }));

  initBodyKine_ = conversions::kinematics::fromSva(realRobot.bodyPosW(mocapBodyName_),
                                                   stateObservation::kine::Kinematics::Flags::pose);
  currentWorldBodyKine_ = initBodyKine_;

  extractTransformFromMocap();
}

bool MocapVisualizer::run(const mc_control::MCController & ctl)
{
  const auto & realRobot = ctl.realRobot(robot_);

  currentWorldBodyKine_ = initBodyKine_ * mocapMappedData_.at(currentIter_);

  stateObservation::kine::Kinematics worldFbKineRealRobot =
      conversions::kinematics::fromSva(realRobot.posW(), stateObservation::kine::Kinematics::Flags::pose);
  stateObservation::kine::Kinematics worldBodyKineRealRobot = conversions::kinematics::fromSva(
      realRobot.bodyPosW(mocapBodyName_), stateObservation::kine::Kinematics::Flags::pose);
  bodyFbKine_ = worldBodyKineRealRobot.getInverse() * worldFbKineRealRobot;

  stateObservation::kine::Kinematics worldFbKine = currentWorldBodyKine_ * bodyFbKine_;

  X_0_fb_.translation() = worldFbKine.position();
  X_0_fb_.rotation() = worldFbKine.orientation.toMatrix3().transpose();

  my_robots_->robot().mbc().q = ctl.realRobot().mbc().q;
  update(my_robots_->robot());
  updateContacts(ctl);

  currentIter_++;
  currentMocapDataTime_ += ctl.timeStep;

  return true;
}

const stateObservation::kine::Kinematics & MocapVisualizer::getContactKinematics(MocapContact & contact,
                                                                                 const mc_rbdyn::ForceSensor & fs)
{
  const auto & mocapRobot = my_robots_->robot();

  if(contactsManager_.getContactsDetection() == measurements::ContactsManager<MocapContact>::ContactsDetection::Sensors)
  {
    // If the contact is detecting using thresholds, we will then consider the sensor frame as
    // the contact surface frame directly.
    const sva::PTransformd & bodyContactSensorPose = fs.X_p_f();
    stateObservation::kine::Kinematics bodyContactSensorKine =
        conversions::kinematics::fromSva(bodyContactSensorPose, stateObservation::kine::Kinematics::Flags::vel);

    // kinematics of the sensor's parent body in the world
    stateObservation::kine::Kinematics worldBodyKine =
        conversions::kinematics::fromSva(mocapRobot.mbc().bodyPosW[mocapRobot.bodyIndexByName(fs.parentBody())],
                                         stateObservation::kine::Kinematics::Flags::pose);

    contact.worldKine_ = worldBodyKine * bodyContactSensorKine;
  }
  else // the kinematics of the contact are the ones of the associated surface
  {
    contact.worldKine_ = conversions::kinematics::fromSva(mocapRobot.surfacePose(contact.surface()),
                                                          stateObservation::kine::Kinematics::Flags::vel);
  }

  return contact.worldKine_;
}

///////////////////////////////////////////////////////////////////////
/// -------------------------Called functions--------------------------
///////////////////////////////////////////////////////////////////////

void MocapVisualizer::update(mc_control::MCController &) // this function is called by the pipeline if the
                                                         // update is set to true in the configuration file
{
}

void MocapVisualizer::update(mc_rbdyn::Robot & robot)
{
  robot.posW(X_0_fb_);
}

void MocapVisualizer::updateContacts(const mc_control::MCController & ctl)
{
  auto & logger = (const_cast<mc_control::MCController &>(ctl)).logger();

  auto onNewContact = [this, &ctl, &logger](MocapContact & newContact)
  {
    getContactKinematics(newContact, ctl.robot().forceSensor(newContact.forceSensor()));
    addContactsLogs(newContact, logger);
  };

  auto onMaintainedContact = [this, &ctl](MocapContact & maintainedContact)
  { getContactKinematics(maintainedContact, ctl.robot().forceSensor(maintainedContact.forceSensor())); };

  auto onRemovedContact = [&logger](MocapContact & removedContact) { logger.removeLogEntries(&removedContact); };

  contactsManager_.updateContacts(ctl, robot_, onNewContact, onMaintainedContact, onRemovedContact);
}
///////////////////////////////////////////////////////////////////////
/// -------------------------------Logs--------------------------------
///////////////////////////////////////////////////////////////////////

void MocapVisualizer::addToLogger(const mc_control::MCController &,
                                  mc_rtc::Logger & logger,
                                  const std::string & category)
{
  logger.addLogEntry(category + "_mocap_fb_posW", [this]() -> const sva::PTransformd & { return X_0_fb_; });
  logger.addLogEntry(category + "_mocap_fb_yaw",
                     [this]() -> double
                     { return -stateObservation::kine::rotationMatrixToYawAxisAgnostic(X_0_fb_.rotation()); });
  logger.addLogEntry(category + "_mocap_bodyFbPose_ori",
                     [this]() -> const Eigen::Quaterniond { return bodyFbKine_.orientation.toQuaternion().inverse(); });
  logger.addLogEntry(category + "_mocap_bodyFbPose_pos",
                     [this]() -> const Eigen::Vector3d & { return bodyFbKine_.position(); });
}

void MocapVisualizer::addContactsLogs(MocapContact & contact, mc_rtc::Logger & logger)
{
  logger.addLogEntry("MocapVisualizer_contacts_" + contact.name() + "_position", &contact,
                     [&contact]() -> stateObservation::Vector3 { return contact.worldKine_.position(); });
  logger.addLogEntry("MocapVisualizer_contacts_" + contact.name() + "_orientation", &contact,
                     [&contact]() -> stateObservation::Quaternion
                     { return contact.worldKine_.orientation.toQuaternion().inverse(); });
}

void MocapVisualizer::removeFromLogger(mc_rtc::Logger &, const std::string &) {}

void MocapVisualizer::addToGUI(const mc_control::MCController &,
                               mc_rtc::gui::StateBuilder &,
                               const std::vector<std::string> &)
{
  using namespace mc_rtc::gui;
  // clang-format off

  // clang-format on
}

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

void MocapVisualizer::extractTransformFromMocap()
{
  std::string fname;
  fname = csvPath_;

  std::vector<std::string> row;

  std::string line, word;

  std::fstream file(fname, std::ios::in);

  if(file.is_open())
  {
    int i = 0;
    stateObservation::kine::Kinematics currentKine;

    // Ignore the first line containing headers
    std::getline(file, line);

    while(getline(file, line))
    {
      row.clear();
      std::stringstream str(line);

      while(getline(str, word, ',')) row.push_back(word);

      currentKine.position.set()(0) = std::stod(row.at(1));
      currentKine.position.set()(1) = std::stod(row.at(2));
      currentKine.position.set()(2) = std::stod(row.at(3));

      stateObservation::Vector4 quat;

      quat(0) = std::stod(row.at(7)); // w
      quat(1) = std::stod(row.at(4)); // x
      quat(2) = std::stod(row.at(5)); // y
      quat(3) = std::stod(row.at(6)); // z

      currentKine.orientation = Eigen::Quaterniond(quat(0), quat(1), quat(2), quat(3)).normalized();

      mocapMappedData_.insert(std::make_pair(i, currentKine));

      i++;
    }
  }
  else { mc_rtc::log::error_and_throw<std::runtime_error>("Could not open the file\n"); }

  stateObservation::kine::Kinematics initKineT = mocapMappedData_.at(0).getInverse();
  for(int j = 0; j < mocapMappedData_.size(); j++) { mocapMappedData_.at(j) = initKineT * mocapMappedData_.at(j); }
}

} // namespace mc_state_observation

EXPORT_OBSERVER_MODULE("MocapVisualizer", mc_state_observation::MocapVisualizer)