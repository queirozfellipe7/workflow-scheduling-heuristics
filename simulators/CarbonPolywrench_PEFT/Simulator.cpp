// Arquivo: src/Simulator.cpp

#include <iostream>
#include <vector>
#include <wrench-dev.h>
#include "Controller.h"
#include "host_carbon_footprint.h"

int main(int argc, char **argv) {
    auto simulation = wrench::Simulation::createSimulation();
    simulation->init(&argc, argv);

    sg_host_carbon_footprint_plugin_init();
    std::cout << "INFO: Plugin de pegada de carbono inicializado." << std::endl;

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <xml platform file> <workflow json file>" << std::endl;
        exit(1);
    }

    simulation->instantiatePlatform(argv[1]);
    std::string workflow_file = argv[2];

    // O serviço de armazenamento está no host principal
    auto storage_service = simulation->add(wrench::SimpleStorageService::createSimpleStorageService("DGX_System", {"/"}, {}, {}));

    // *** CORREÇÃO IMPORTANTE: Lista de todos os hosts que podem computar ***
    std::vector<std::string> compute_hosts = {"DGX_System", "GPU1", "GPU2", "GPU3", "GPU4", "GPU5", "GPU6", "GPU7", "GPU8"};

    // O serviço de computação é gerenciado a partir do host principal
    auto baremetal_service = simulation->add(new wrench::BareMetalComputeService("DGX_System", compute_hosts, "", {}));
    
    // O controller também roda no host principal
    auto controller = simulation->add(new Controller(baremetal_service, storage_service, "DGX_System", workflow_file));

    simulation->launch();

    std::cout << "INFO: Simulação concluída." << std::endl;

    // Seção de resultados de carbono
    std::cout << "\n==============================" << std::endl;
    std::cout << "  Resultados da Pegada de Carbono" << std::endl;
    std::cout << "==============================" << std::endl;
    std::vector<simgrid::s4u::Host*> host_list = simgrid::s4u::Engine::get_instance()->get_all_hosts();
    for (auto const& host : host_list) {
        if (dynamic_cast<simgrid::s4u::VirtualMachine*>(host) == nullptr) {
            double carbon = sg_host_get_carbon_footprint(host);
            printf("Host %-15s: %10.2f gCO2\n", host->get_cname(), carbon);
        }
    }
    std::cout << "==============================" << std::endl;

    return 0;
}
