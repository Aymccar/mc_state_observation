#include"mc_state_observation/measurement/SchimdtTrigger.h"


namespace mc_state_observation{

    SchmidtTrigger::SchmidtTrigger(const mc_control::ContactSet& contacts):ContactManager(contacts){}

    SchmidtTrigger::SchmidtTrigger(const mc_control::ContactSet& contacts, ... config):ContactManager(contacts){
        setConfig(config);
    }

    void SchmidtTrigger::setConfig(...){
        lowerThreshold =...;
        upperThreshold =...;
    }


    void SchmidtTrigger::run(const mc_control::MCController& ctl){
        for (auto it = contacts.begin(); it != contacts.end(); it++){
            const Vector& meas = ctl.contacts().find(*it).getInfo().getMeasurement();//Get the measurement of the specified contact
            double force = meas(0); //This Schimdt trigger is based on force only. Maybe change th name
            //Hysteresis
            if (it->getInfo().getIsSet()){
                // If the contact is already set :
                //cond
                //copy global contact in local
                //change status on local contacts
            }
            else {
                // If the contact isn't set yet :
                //cond
                //copy global contact in local
                //change status on local contacts
            }
        }
    }

    void SchimdtTrigger::update(mc_control::MCController& ctl){

        //set global contacts according to local
    }
}
