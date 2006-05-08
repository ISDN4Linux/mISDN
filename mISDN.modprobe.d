install hfcsusb /etc/init.d/misdn-init start hfcsusb

remove  hfcsusb if  ps ax | grep -v grep | grep asterisk > /dev/null ; then asterisk -rx "stop now" ; fi  && /etc/init.d/misdn-init stop 


