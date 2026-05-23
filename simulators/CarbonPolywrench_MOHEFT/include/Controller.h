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

    // Estruturas necessárias para MOHEFT
    struct Solution {
        std::map<std::string, std::string> assignment;      
        double makespan = 0.0;
        double energy_joules = 0.0;
        double carbon_gco2 = 0.0;
        std::map<std::string, double> finish_time;         
        std::map<std::string, double> host_avail;          
    };

    bool dominates(const Solution& a, const Solution& b) const;
    Solution select_best_solution(const std::vector<Solution>& pareto_front) const;
};

#endif // CONTROLLER_H