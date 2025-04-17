#pragma once

#include "DriverInfo.h"

class DriverController
{	
private:

public:
	bool loadAsioDriver(char* name);
	long init_asio_static_data(DriverInfo* asioDriverInfo);
	ASIOError create_asio_buffers(DriverInfo* asioDriverInfo);
	unsigned long get_sys_reference_time();

};

