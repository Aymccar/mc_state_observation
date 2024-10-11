
#include"mc_state_observation/ContactObserver.h"


namespace mc_state_observation {


    ContactOberver::ContactObserver(){}

    void ContactObserver::configure(const mc_control::MCController & ctl, const mc_rtc::Configuration&){
        auto algo = config("DetectionAlgorithm", "");

        if (algo == ""){
            //TODO raise excep no algorithm specified
        }
        else if (algo == "Threshold"){
            //TODO Get args for contactManager. Lot of possibility

            contactManager = new Threshold();
        }
        else if (algo == "SchmidtTrigger"){

            contactManager = new SchmidtTrigger();
            contactManager->setConfig(...)
        }
        else if (algo == "HMM"){

            contactManager = new HMM(); 
        }

        else{
            //TODO raise excep not a knowm algorithm has to be implemented ^^
        }
    }

    void reset(const mc_control::MCController& ctl){
    }

    void ContactObserver::run(const mc_control::MCController& ctl){
        contactManager->run(ctl);
    }


    void ContactObserver::update(mc_control::MCController& ctl){
        contactManager->update()
    }

}
