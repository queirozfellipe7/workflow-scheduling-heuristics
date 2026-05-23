

#include <simgrid/s4u/Engine.hpp>
#include <simgrid/s4u/Host.hpp>
#include <simgrid/s4u/VirtualMachine.hpp>
#include <simgrid/s4u/Exec.hpp>
#include <simgrid/Exception.hpp>
#include <simgrid/plugins/energy.h>
#include "simgrid/simcall.hpp"

#include "host_carbon_footprint.h"

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(host_carbon_footprint, kernel, "Logs para o plugin de pegada de carbono");

namespace simgrid::plugin {

class HostCarbonFootprint {
public:
    static simgrid::xbt::Extension<simgrid::s4u::Host, HostCarbonFootprint> EXTENSION_ID;

    explicit HostCarbonFootprint(simgrid::s4u::Host* ptr);
    ~HostCarbonFootprint() = default;

    void update();
    double get_carbon_footprint();

private:
    simgrid::s4u::Host* const host_;
    double total_carbon_footprint_ = 0.0;
    double current_carbon_intensity_ = 0.0;
    double last_energy_joules_ = 0.0; 
    double last_updated_ = 0.0;
};

simgrid::xbt::Extension<simgrid::s4u::Host, HostCarbonFootprint> HostCarbonFootprint::EXTENSION_ID;

HostCarbonFootprint::HostCarbonFootprint(simgrid::s4u::Host* ptr) : host_(ptr) {
    const char* intensity_str = host_->get_property("carbon_intensity");
    if (intensity_str) {
        try {
            current_carbon_intensity_ = std::stod(intensity_str);
        } catch (const std::invalid_argument&) {
            XBT_WARN("Valor inválido para 'carbon_intensity' no host %s. Usando 0.", host_->get_cname());
        }
    }
    last_updated_ = simgrid::s4u::Engine::get_clock();
    last_energy_joules_ = sg_host_get_consumed_energy(host_); 
}

void HostCarbonFootprint::update() {
    const double now = simgrid::s4u::Engine::get_clock();
    if (now <= last_updated_) return;

    
    double total_energy_now = sg_host_get_consumed_energy(host_);
    double energy_this_step_joules = total_energy_now - last_energy_joules_;
    double energy_this_step_kwh = energy_this_step_joules / 3.6e6;

    total_carbon_footprint_ += energy_this_step_kwh * current_carbon_intensity_;

    last_energy_joules_ = total_energy_now;
    last_updated_ = now;
}

double HostCarbonFootprint::get_carbon_footprint() {
    simgrid::kernel::actor::simcall_answered(std::bind(&HostCarbonFootprint::update, this));
    return total_carbon_footprint_;
}

} 

using simgrid::plugin::HostCarbonFootprint;



static void on_creation(simgrid::s4u::Host& host) {
    if (not dynamic_cast<simgrid::s4u::VirtualMachine*>(&host)) {
        host.extension_set(new HostCarbonFootprint(&host));
    }
}

static void on_host_change(simgrid::s4u::Host const& host) {
    auto* pm = &host;
    if (auto const* vm = dynamic_cast<simgrid::s4u::VirtualMachine const*>(pm)) {
        pm = vm->get_pm();
    }
    if (pm) {
        pm->extension<HostCarbonFootprint>()->update();
    }
}



static void ensure_plugin_inited() {
    if (not HostCarbonFootprint::EXTENSION_ID.valid()) {
        throw simgrid::xbt::InitializationError("O plugin de Carbono não está ativo. Chame sg_host_carbon_footprint_plugin_init() primeiro.");
    }
}

void sg_host_carbon_footprint_plugin_init() {
    if (HostCarbonFootprint::EXTENSION_ID.valid()) return;

    sg_host_energy_plugin_init();
    HostCarbonFootprint::EXTENSION_ID = simgrid::s4u::Host::extension_create<HostCarbonFootprint>();

    simgrid::s4u::Host::on_creation_cb(&on_creation);
    
    simgrid::s4u::Host::on_onoff_cb(&on_host_change);
    simgrid::s4u::Host::on_speed_change_cb(&on_host_change);
    
}

double sg_host_get_carbon_footprint(const_sg_host_t host) {
    ensure_plugin_inited();
    auto* pm = host;
    if (auto const* vm = dynamic_cast<simgrid::s4u::VirtualMachine const*>(pm)) {
        pm = vm->get_pm();
    }
    return pm->extension<HostCarbonFootprint>()->get_carbon_footprint();
}


void sg_host_carbon_footprint_load_trace_file(const char* trace_file) {
    
}
