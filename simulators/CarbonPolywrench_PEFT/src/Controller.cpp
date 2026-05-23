#include "Controller.h"
#include <fstream>
#include <limits>
#include <algorithm>
#include <functional>
#include <iomanip>
#include "json.hpp"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

using json = nlohmann::json;

Controller::Controller(
    const std::shared_ptr<wrench::BareMetalComputeService> &bare_metal_compute_service,
    const std::shared_ptr<wrench::SimpleStorageService> &storage_service,
    const std::string &hostname,
    const std::string &json_file_path
) : wrench::ExecutionController(hostname, "controller"),
    bare_metal_compute_service(bare_metal_compute_service),
    storage_service(storage_service),
    json_file_path(json_file_path) {}


std::map<std::pair<std::string, std::string>, double> Controller::compute_oct(
    const std::map<std::string, double>& comp_cost,
    const std::map<std::string, std::vector<std::string>>& successors,
    const std::vector<std::string>& host_list,
    const std::map<std::string, double>& hosts_capabilities,
    const std::vector<std::string>& task_order_reverse  // ordem topológica reversa
) {
    std::map<std::pair<std::string, std::string>, double> oct;

    for (const auto& task : task_order_reverse) {
        for (const auto& host : host_list) {
            auto key = std::make_pair(task, host);
            double w_ij = (comp_cost.at(task) > 0) ? comp_cost.at(task) / hosts_capabilities.at(host) : 0.0;

            double max_succ = 0.0;
            for (const auto& succ : successors.at(task)) {
                double min_over_pm = std::numeric_limits<double>::max();
                for (const auto& pm : host_list) {
                    // c_hat = 0 (sem comunicação explícita no seu modelo)
                    double c_hat = 0.0;
                    auto succ_key = std::make_pair(succ, pm);
                    min_over_pm = std::min(min_over_pm, c_hat + oct[succ_key]);
                }
                max_succ = std::max(max_succ, min_over_pm);
            }
            oct[key] = w_ij + max_succ;
        }
    }
    return oct;
}

int Controller::main() {
    wrench::TerminalOutput::setThisProcessLoggingColor(wrench::TerminalOutput::COLOR_GREEN);
    WRENCH_INFO("Controller starting (PEFT Scheduler)");

    
    double start_time = this->getSimulation()->getCurrentSimulatedDate();

    
    std::ifstream f(json_file_path);
    if (!f.is_open()) {
        WRENCH_WARN("Could not open JSON file: %s", json_file_path.c_str());
        return 1;
    }
    json data = json::parse(f);

    
    std::map<std::string, std::shared_ptr<wrench::DataFile>> files;
    for (auto const &file : data["files"]) {
        std::string file_id = file["id"];
        sg_size_t file_size = file["size"].get<sg_size_t>();
        files[file_id] = wrench::Simulation::addFile(file_id, file_size);
        wrench::StorageService::createFileAtLocation(
            wrench::FileLocation::LOCATION(this->storage_service, files[file_id])
        );
    }

    auto job_manager = this->createJobManager();

    
    std::map<std::string, std::shared_ptr<wrench::CompoundJob>> jobs;
    std::map<std::string, double> comp_cost;
    std::map<std::string, std::vector<std::string>> successors;
    std::map<std::string, std::vector<std::string>> predecessors;
    for (auto const &task : data["jobs"]) {
        std::string id = task["id"];
        auto job = job_manager->createCompoundJob(id);
        jobs[id] = job;
        if (task.contains("file_reads") && !task["file_reads"].empty()) {
            job->addFileReadAction(id, wrench::FileLocation::LOCATION(this->storage_service, files[task["file_reads"][0]["id"]]));
            comp_cost[id] = 0.0;
        } else if (task.contains("file_writes") && !task["file_writes"].empty()) {
            job->addFileWriteAction(id, wrench::FileLocation::LOCATION(this->storage_service, files[task["file_writes"][0]["id"]]));
            comp_cost[id] = 0.0;
        } else {
            int cores = task.contains("cores") ? (int)task["cores"] : 1;
            double flops = task["flops"].get<double>();
            double memory = task.contains("memory") ? task["memory"].get<double>() : 0.0;
            job->addComputeAction(id, flops, memory, 1, cores, wrench::ParallelModel::AMDAHL(0.9));
            comp_cost[id] = flops;
        }
    }
    
    if (data.contains("dependencies")) {
        for (auto const &dep : data["dependencies"]) {
            for (auto const &p : dep["parents"]) {
                for (auto const &c : dep["children"]) {
                    std::string parent = p;
                    std::string child = c;
                    predecessors[child].push_back(parent);
                    successors[parent].push_back(child);
                    jobs[child]->addParentJob(jobs[parent]);
                }
            }
        }
    }
    
    auto hosts_capabilities = bare_metal_compute_service->getCoreFlopRate();
    std::vector<std::string> host_list;
    for (auto const &h : hosts_capabilities) host_list.push_back(h.first);

    
    double avg_flop_rate = 0.0;
    for (auto const &h : hosts_capabilities) avg_flop_rate += h.second;
    if (!hosts_capabilities.empty()) avg_flop_rate /= hosts_capabilities.size();
    else avg_flop_rate = 1.0;

    std::map<std::string, double> rank_u;
    std::function<double(const std::string&)> compute_rank;
    compute_rank = [&](const std::string &id) -> double {
        if (rank_u.count(id)) return rank_u[id];
        double w = (comp_cost[id] > 0) ? comp_cost[id] / avg_flop_rate : 0.0;
        double max_succ = 0.0;
        for (auto const &s : successors[id]) max_succ = std::max(max_succ, compute_rank(s));
        rank_u[id] = w + max_succ;
        return rank_u[id];
    };
    for (auto const &j : jobs) compute_rank(j.first);

   
    std::vector<std::string> task_order;
    for (auto const &j : jobs) task_order.push_back(j.first);
    std::sort(task_order.begin(), task_order.end(), [&](const std::string &a, const std::string &b) {
        return rank_u[a] > rank_u[b];
    });

   
    std::vector<std::string> task_order_reverse = task_order;  // Correção: task_order agora existe
    std::reverse(task_order_reverse.begin(), task_order_reverse.end());  // Ordem reversa para bottom-up

    auto oct_table = compute_oct(comp_cost, successors, host_list, hosts_capabilities, task_order_reverse);

   
    std::map<std::string, double> rank_oct;
    for (const auto& task : task_order) {
        double sum_oct = 0.0;
        for (const auto& host : host_list) {
            auto key = std::make_pair(task, host);  
            sum_oct += oct_table[key];
        }
        rank_oct[task] = sum_oct / host_list.size();  // Média sobre processadores
    }

    // Ordenar tarefas por rank_oct decrescente (substitui a ordenação do HEFT)
    std::sort(task_order.begin(), task_order.end(), [&](const std::string &a, const std::string &b) {
        return rank_oct[a] > rank_oct[b];
    });

    // ---------- 8. PEFT Fase 2: Alocação (igual ao HEFT, mas com nova ordem) ----------
    std::map<std::string, std::string> scheduling_decision;
    std::map<std::string, double> task_finish_time;
    std::map<std::string, double> host_available_time;
    std::map<std::string, double> host_total_load;
    for (auto const &h : hosts_capabilities) {
        host_available_time[h.first] = 0.0;
        host_total_load[h.first] = 0.0;
    }
    for (auto const &id : task_order) {
        double best_eft = std::numeric_limits<double>::max();
        std::string best_host = "";
        double ready_time = 0.0;
        for (auto const &p : predecessors[id]) ready_time = std::max(ready_time, task_finish_time[p]);
        for (auto const &h : hosts_capabilities) {
            std::string hostname = h.first;
            double flop_rate = h.second;
            double execution_time = (comp_cost[id] > 0) ? comp_cost[id] / flop_rate : 0.001;
            double est = std::max(host_available_time[hostname], ready_time);
            double eft = est + execution_time;
            if (eft < best_eft) {
                best_eft = eft;
                best_host = hostname;
            }
        }
        scheduling_decision[id] = best_host;
        task_finish_time[id] = best_eft;
        host_available_time[best_host] = best_eft;
        host_total_load[best_host] += (comp_cost[id] > 0) ? comp_cost[id] / hosts_capabilities[best_host] : 0.0;
    }

    
    std::set<std::string> completed;
    std::set<std::string> submitted;
    std::map<std::string, std::string> actual_execution_host;
    while (completed.size() < jobs.size()) {
        for (auto const &id : task_order) {
            if (submitted.count(id)) continue;
            bool ready = true;
            for (auto const &p : predecessors[id]) {
                if (completed.find(p) == completed.end()) {
                    ready = false;
                    break;
                }
            }
            if (ready) {
                std::map<std::string, std::string> service_args;
                service_args[id] = scheduling_decision[id];
                actual_execution_host[id] = scheduling_decision[id];
                job_manager->submitJob(jobs[id], bare_metal_compute_service, service_args);
                submitted.insert(id);
            }
        }
        if (completed.size() < jobs.size()) {
            auto event = this->waitForNextEvent();
            if (auto ev = std::dynamic_pointer_cast<wrench::CompoundJobCompletedEvent>(event)) {
                completed.insert(ev->job->getName());
            } else if (auto ev_fail = std::dynamic_pointer_cast<wrench::CompoundJobFailedEvent>(event)) {
                WRENCH_WARN("Job %s failed!", ev_fail->job->getName().c_str());
                return 1;
            }
        }
    }
    double end_time = this->getSimulation()->getCurrentSimulatedDate();

    
    std::string csv_filename = "resultados_" + this->getWorkflowName() + "_PEFT.csv";
    std::ofstream csv_file(csv_filename);
   
    if (csv_file.is_open()) {
        csv_file << "task_id,host,finish_time,load_seconds\n";
        for (auto const &id : task_order) {
            csv_file << id << ","
                     << actual_execution_host[id] << ","
                     << std::fixed << std::setprecision(4) << task_finish_time[id] << ","
                     << ((comp_cost[id] > 0) ? comp_cost[id] / hosts_capabilities[actual_execution_host[id]] : 0.0) << "\n";
        }
        csv_file.close();
        WRENCH_INFO("Log detalhado salvo em %s", csv_filename.c_str());
    }
    
    std::cout << "\n============================================================" << std::endl;
    std::cout << " RESUMO DE UTILIZAÇÃO DA PLATAFORMA" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::left << std::setw(20) << "Host" << " | " << "Tempo Total de Uso (s)" << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;
   
    for (auto const &h : hosts_capabilities) {
        std::cout << std::left << std::setw(20) << h.first << " | " 
                  << std::fixed << std::setprecision(4) << host_total_load[h.first] << " s" << std::endl;
    }
   
    std::cout << "------------------------------------------------------------" << std::endl;
    std::cout << "Makespan Total: " << std::fixed << std::setprecision(4) << (end_time - start_time) << " s" << std::endl;
    std::cout << "============================================================" << std::endl;

    return 0;
} 

std::string Controller::getWorkflowName(){
   
    size_t last_slash = json_file_path.find_last_of("/\\");
    std::string name = (last_slash == std::string::npos) ? json_file_path : json_file_path.substr(last_slash + 1);
    
    
    size_t last_dot = name.find_last_of(".");
    if (last_dot != std::string::npos) name = name.substr(0, last_dot);
    
    return name;
}