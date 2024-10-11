#pragma once 
#include"mc_control/MCController.h"


namespace mc_state_observation {

    class ContactObserver {

        public:

            void configure(const mc_control::MCController& ctl, const mc_rtc::Configuration&) override;
            void reset(const mc_control::MCController& ctl) override;
            void run(const mc_control::MCController& ctl) override;
            void update(const mc_control::MCController& ctl) override;

        private:

            ContactManager* contactManager;

    }
}
