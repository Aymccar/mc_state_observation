#pragma once
#include"mc_control/MCController.h"

namespace mc_state_observation{

    using mc_control::ContactSet = typedef std::unordered_set<Contact, std::hash<Contact>, std::equal_to<Contact>, Eigen::aligned_allocator<Contact> >

        class ContactManager{
            public :

                ContactManager::ContactManager(const mc_control::ContactSet& contacts_controller):contacts(contact_controller){};
                virtual void setConfig(...);
                virtual void run(const mc_control::MCController& ctl) = 0;
                virtual void update(mc_control::MCController& ctl) = 0;    

                private

                    mc_control::ContactSet contacts; 
        };



}
