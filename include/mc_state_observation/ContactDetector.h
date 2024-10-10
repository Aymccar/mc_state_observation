#pragma once

#include <mc_observers/Observer.h>
#include <mc_state_observation/measurements/ContactWithSensor.h>
#include <mc_state_observation/measurements/ContactsManager.h>
#include <state-observation/observer/hmm-contact.hpp>

namespace mc_state_observation
{

struct ContactDetector : public mc_observers::Observer
{
public:
  ContactDetector(const std::string & type, double dt);

  void configure(const mc_control::MCController & ctl, const mc_rtc::Configuration &) override;

  void reset(const mc_control::MCController & ctl) override;

  bool run(const mc_control::MCController & ctl) override;

  void update(mc_control::MCController & ctl) override;

protected:
  /*! \brief Add observer from logger
   *
   * @param category Category in which to log this observer
   */
  void addToLogger(const mc_control::MCController &, mc_rtc::Logger &, const std::string & category) override;

  /*! \brief Remove observer from logger
   *
   * @param category Category in which this observer entries are logged
   */
  void removeFromLogger(mc_rtc::Logger &, const std::string & category) override;

  /*! \brief Add observer information the GUI.
   *
   * @param category Category in which to add this observer
   */
  void addToGUI(const mc_control::MCController &,
                mc_rtc::gui::StateBuilder &,
                const std::vector<std::string> & /* category */) override;

protected:
  /// @{
  std::string robot_ = ""; ///< Name of robot
  std::unordered_map<std::string, measurements::ContactWithSensor> contacts_;

  /// initialization of the extended Kalman filter
  stateObservation::Hmm_contact_force hmm_;
};

} // namespace mc_state_observation