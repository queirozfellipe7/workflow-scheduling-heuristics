#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <wrench-dev.h>
#include <map>
#include <vector>
#include <string>

class Controller : public wrench::ExecutionController {
public:
    Controller(const std::shared_ptr<wrench::BareMetalComputeService> &bare_metal_compute_service,
               const std::shared_ptr<wrench::SimpleStorageService> &storage_service,
               const std::string &hostname,
               const std::string &json_file_path);

    int main() override;
    std::string getWorkflowName();

private:
    std::shared_ptr<wrench::BareMetalComputeService> bare_metal_compute_service;
    std::shared_ptr<wrench::SimpleStorageService> storage_service;
    std::string json_file_path;

    // Função para computar OCT (mantida para PEFT)
    std::map<std::pair<std::string, std::string>, double> compute_oct(
        const std::map<std::string, double>& comp_cost,
        const std::map<std::string, std::vector<std::string>>& successors,
        const std::vector<std::string>& host_list,
        const std::map<std::string, double>& hosts_capabilities,
        const std::vector<std::string>& task_order_reverse  // ordem topológica reversa
    );
};

#endif // CONTROLLER_H