

#ifndef HOST_CARBON_FOOTPRINT_H
#define HOST_CARBON_FOOTPRINT_H

#include <wrench.h> 
void sg_host_carbon_footprint_plugin_init();


double sg_host_get_carbon_footprint(const_sg_host_t host);


void sg_host_carbon_footprint_load_trace_file(const char* trace_file);

#endif // HOST_CARBON_FOOTPRINT_H
