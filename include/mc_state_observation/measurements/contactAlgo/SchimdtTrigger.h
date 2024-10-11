#pragma once
#include"mc_state_observatiom/measurement/contactAlgo/ContactManager.h"

namespace mc_state_observation {

    using mc_control::ContactSet = typedef std::unordered_set<Contact, std::hash<Contact>, std::equal_to<Contact>, Eigen::aligned_allocator<Contact> >


        class SchmidtTrigger : public ContactManager {

            public :
                SchmidtTrigger(const mc_control::ContactSet& contacts);
                SchmidtTrigger(const mc_control::ContactSet& contacts, ... condig);
                void setConfig(/*TODO Se mettre d'qccord pour les args*/);

                void run(const mc_control::MCController& ctl) override;
                void update(mc_control::MCController& ctl) override;    

            private :

                double lowerThreshold;
                double upperThreshold;

        }
}
