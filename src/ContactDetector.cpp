#include <mc_control/Contact.h>
#include <mc_control/MCController.h>
#include <mc_observers/ObserverMacros.h>
#include <mc_rbdyn/Contact.h>
#include <mc_state_observation/ContactDetector.h>
#include <mc_state_observation/gui_helpers.h>

namespace mc_state_observation
{
typedef std::unordered_map<std::string, measurements::ContactWithSensor> ContactsMap;
ContactDetector::ContactDetector(const std::string & type, double dt) : mc_observers::Observer(type, dt), hmm_(4, 2)
{
  /// initialization of the HMM
}

ContactsMap & getContactsMap(mc_control::MCController & ctl)
{
  return ctl.datastore().get<ContactsMap>("observers_contactMap");
}

void ContactDetector::configure(const mc_control::MCController & ctl, const mc_rtc::Configuration & config)
{
  robot_ = config("robot", ctl.robot().name());
  detectionAlgo_ = config("detectionAlgo");

  auto & datastore = (const_cast<mc_control::MCController &>(ctl)).datastore();
  datastore.make<ContactsMap>("observers_contactMap", ContactsMap());
}

void ContactDetector::reset(const mc_control::MCController & ctl) {}

bool ContactDetector::run(const mc_control::MCController & ctl) {}

void ContactDetector::update(mc_control::MCController & ctl) {}

void ContactDetector::addToLogger(const mc_control::MCController &,
                                  mc_rtc::Logger & logger,
                                  const std::string & category)
{
}

void ContactDetector::removeFromLogger(mc_rtc::Logger & logger, const std::string & category) {}

void ContactDetector::addToGUI(const mc_control::MCController & ctl,
                               mc_rtc::gui::StateBuilder & gui,
                               const std::vector<std::string> & category)
{
}

} // namespace mc_state_observation
EXPORT_OBSERVER_MODULE("ContactDetector", mc_state_observation::ContactDetector)
