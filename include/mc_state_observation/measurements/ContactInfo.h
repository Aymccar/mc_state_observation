#pragma once
#include<string>


namespace mc_state_observation::measurements
{
    class ContactInfo {
        public :

           virtual const Vector& getMeasurement() const {return measurement} = 0;//? return &
           virtual void setMeasurement(const Vector& meas){measurement = meas} = 0;

           virtual void setIsSet(const bool status){isSet = status};
           virtual bool getIsSet() const{return isSet};
           
           virtual std::shared_ptr<ContactInfo> copy() = 0; 
        private :
            
           Vector Measurement;
           bool isSet;
    };
}
