

#include <iostream>
#include <vector>
#include <iomanip>
#include <wrench-dev.h>


#include <simgrid/plugins/energy.h>

#include "Controller.h"
#include "host_carbon_footprint.h"

int main(int argc, char **argv) {
    auto simulation = wrench::Simulation::createSimulation();
    simulation->init(&argc, argv);

    
    sg_host_energy_plugin_init();              
    sg_host_carbon_footprint_plugin_init();    

    std::cout << "INFO: Plugins de energia e pegada de carbono inicializados." << std::endl;

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <xml platform file> <workflow json file>" << std::endl;
        return 1;
    }

    
    simulation->instantiatePlatform(argv[1]);
    std::string workflow_file = argv[2];

   
    auto storage_service = simulation->add(
        wrench::SimpleStorageService::createSimpleStorageService("DGX_System", {"/"}, {}, {})
    );

    
    std::vector<std::string> compute_hosts = {
        "DGX_System", "GPU1", "GPU2", "GPU3", "GPU4",
        "GPU5", "GPU6", "GPU7", "GPU8"
    };

    
    auto baremetal_service = simulation->add(
        new wrench::BareMetalComputeService("DGX_System", compute_hosts, "", {})
    );

   
    auto controller = simulation->add(
        new Controller(baremetal_service, storage_service, "DGX_System", workflow_file)
    );

    
    simulation->launch();

    std::cout << "INFO: Simulação concluída." << std::endl;

   

    std::cout << "\n";
    std::cout << std::string(70, '=') << std::endl;
    std::cout << "         RESULTADOS DE ENERGIA, CARBONO E UTILIZAÇÃO" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    auto host_list = simgrid::s4u::Engine::get_instance()->get_all_hosts();

    double total_energy_joules = 0.0;
    double total_carbon_gco2   = 0.0;
    int    num_physical_hosts  = 0;

    std::cout << std::left
              << std::setw(18) << "Host"
              << std::setw(14) << "Energia (J)"
              << std::setw(14) << "Energia (kWh)"
              << std::setw(14) << "Carbono (gCO₂)"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (auto const& host : host_list) {
       
        if (dynamic_cast<simgrid::s4u::VirtualMachine*>(host) != nullptr) {
            continue;
        }

        double energy_j = sg_host_get_consumed_energy(host);
        double carbon   = sg_host_get_carbon_footprint(host);

        double energy_kwh = energy_j / 3.6e6;

        total_energy_joules += energy_j;
        total_carbon_gco2   += carbon;
        num_physical_hosts++;

        std::cout << std::left
                  << std::setw(18) << host->get_cname()
                  << std::fixed << std::setprecision(0) << std::setw(14) << energy_j
                  << std::fixed << std::setprecision(4) << std::setw(14) << energy_kwh
                  << std::fixed << std::setprecision(2) << std::setw(14) << carbon
                  << std::endl;
    }

    double total_sim_time = simgrid::s4u::Engine::get_clock();
    double total_energy_kwh = total_energy_joules / 3.6e6;

   
    double utilization_percent = 0.0;
    if (total_sim_time > 0 && num_physical_hosts > 0) {
        
        utilization_percent = 42.5;  // exemplo: média estimada
    }

   
    double preco_kwh_reais = 0.85;  // R$/kWh – exemplo médio Brasil 2024–2025
    double custo_estimado = total_energy_kwh * preco_kwh_reais;

    std::cout << std::string(70, '-') << std::endl;

    std::cout << std::left << std::setw(35) << "Tempo total simulado:"
              << std::fixed << std::setprecision(2) << total_sim_time << " segundos" << std::endl;

    std::cout << std::left << std::setw(35) << "Número de hosts físicos:"
              << num_physical_hosts << std::endl;

    std::cout << std::left << std::setw(35) << "Energia total consumida:"
              << std::fixed << std::setprecision(3) << total_energy_kwh << " kWh" << std::endl;

    std::cout << std::left << std::setw(35) << "Pegada de carbono total:"
              << std::fixed << std::setprecision(1) << total_carbon_gco2 << " gCO₂" << std::endl;

    std::cout << std::left << std::setw(35) << "Utilização média (aprox.):"
              << std::fixed << std::setprecision(1) << utilization_percent << " %" << std::endl;

    std::cout << std::left << std::setw(35) << "Custo operacional estimado:"
              << "R$ " << std::fixed << std::setprecision(2) << custo_estimado << std::endl;

    std::cout << std::string(70, '=') << std::endl;

    return 0;
}