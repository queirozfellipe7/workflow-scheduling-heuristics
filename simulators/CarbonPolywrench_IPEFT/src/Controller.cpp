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


std::map<std::pair<std::string, std::string>, double> Controller::compute_pct(
    const std::map<std::string, double>& comp_cost,
    const std::map<std::string, std::vector<std::string>>& successors,
    const std::vector<std::string>& host_list,
    const std::map<std::string, double>& hosts_capabilities,
    const std::vector<std::string>& task_order_reverse
) {
    std::map<std::pair<std::string, std::string>, double> pct;

    for (const auto& task : task_order_reverse) {
        for (const auto& host : host_list) {
            auto key = std::make_pair(task, host);
            double w_ij = 0.0;
            if (comp_cost.count(task) && hosts_capabilities.count(host) && comp_cost.at(task) > 0) {
                w_ij = comp_cost.at(task) / hosts_capabilities.at(host);
            }

            double max_succ = 0.0;
            if (successors.count(task)) {
                for (const auto& succ : successors.at(task)) {
                    double max_over_pm = std::numeric_limits<double>::min();
                    for (const auto& pm : host_list) {
                        double c_hat = 0.0;
                        auto succ_key = std::make_pair(succ, pm);
                        double succ_value = pct.count(succ_key) ? pct.at(succ_key) : 0.0;
                        max_over_pm = std::max(max_over_pm, c_hat + succ_value);
                    }
                    max_succ = std::max(max_succ, max_over_pm);
                }
            }
            pct[key] = w_ij + max_succ;
        }
    }
    return pct;
}

void Controller::compute_aest_alst(
    const std::map<std::string, double>& comp_cost,
    const std::map<std::string, std::vector<std::string>>& predecessors,
    const std::map<std::string, std::vector<std::string>>& successors,
    const std::vector<std::string>& host_list,
    const std::map<std::string, double>& hosts_capabilities,
    std::map<std::string, double>& aest,
    std::map<std::string, double>& alst,
    const std::vector<std::string>& task_order
) {
    
    for (const auto& task : task_order) {
        double min_exec = std::numeric_limits<double>::max();
        for (const auto& host : host_list) {
            if (comp_cost.count(task) && hosts_capabilities.count(host) && comp_cost.at(task) > 0) {
                min_exec = std::min(min_exec, comp_cost.at(task) / hosts_capabilities.at(host));
            }
        }
        double max_pred = 0.0;
        if (predecessors.count(task)) {
            for (const auto& p : predecessors.at(task)) {
                if (aest.count(p)) max_pred = std::max(max_pred, aest.at(p));
            }
        }
        aest[task] = max_pred + min_exec;
    }

    
    double max_makespan = 0.0;
    for (const auto& task : task_order) {
        if (aest.count(task)) max_makespan = std::max(max_makespan, aest.at(task));
    }

    for (auto rit = task_order.rbegin(); rit != task_order.rend(); ++rit) {
        const auto& task = *rit;
        double min_exec = std::numeric_limits<double>::max();
        for (const auto& host : host_list) {
            if (comp_cost.count(task) && hosts_capabilities.count(host) && comp_cost.at(task) > 0) {
                min_exec = std::min(min_exec, comp_cost.at(task) / hosts_capabilities.at(host));
            }
        }
        double min_succ = max_makespan;
        if (successors.count(task)) {
            for (const auto& s : successors.at(task)) {
                if (alst.count(s)) min_succ = std::min(min_succ, alst.at(s));
            }
        }
        alst[task] = min_succ - min_exec;
    }
}


std::map<std::pair<std::string, std::string>, double> Controller::compute_cnct(
    const std::set<std::string>& critical_nodes,
    const std::map<std::string, std::vector<std::string>>& successors,
    const std::vector<std::string>& host_list,
    const std::map<std::pair<std::string, std::string>, double>& pct_table,
    const std::vector<std::string>& task_order
) {
    std::map<std::pair<std::string, std::string>, double> cnct;

    for (const auto& task : task_order) {
        for (const auto& host : host_list) {
            auto key = std::make_pair(task, host);
            double max_succ = 0.0;
            if (successors.count(task)) {
                for (const auto& succ : successors.at(task)) {
                    if (critical_nodes.count(succ)) {
                        double min_over_pm = std::numeric_limits<double>::max();
                        for (const auto& pm : host_list) {
                            double c_hat = 0.0;
                            auto succ_key = std::make_pair(succ, pm);
                            double succ_value = pct_table.count(succ_key) ? pct_table.at(succ_key) : 0.0;
                            min_over_pm = std::min(min_over_pm, c_hat + succ_value);
                        }
                        max_succ = std::max(max_succ, min_over_pm);
                    }
                }
            }
            cnct[key] = max_succ;
        }
    }
    return cnct;
}

int Controller::main() {
    wrench::TerminalOutput::setThisProcessLoggingColor(wrench::TerminalOutput::COLOR_GREEN);
    WRENCH_INFO("Controller starting (IPEFT Scheduler)");

    double start_time = this->getSimulation()->getCurrentSimulatedDate();

    // 1. Leitura do JSON
    std::ifstream f(json_file_path);
    if (!f.is_open()) {
        WRENCH_WARN("Could not open JSON file: %s", json_file_path.c_str());
        return 1;
    }
    json data = json::parse(f);

    // 2. Criação dos arquivos
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

    // 6. Cálculo prévio de avg_flop_rate
    double avg_flop_rate = 0.0;
    for (auto const &h : hosts_capabilities) avg_flop_rate += h.second;
    if (!hosts_capabilities.empty()) avg_flop_rate /= hosts_capabilities.size();
    else avg_flop_rate = 1.0;

    std::map<std::string, double> rank_u;
    std::function<double(const std::string&)> compute_rank = [&](const std::string &id) -> double {
        if (rank_u.count(id)) return rank_u[id];
        double w = (comp_cost.count(id) && comp_cost.at(id) > 0) ? comp_cost.at(id) / avg_flop_rate : 0.0;
        double max_succ = 0.0;
        if (successors.count(id)) {
            for (auto const &s : successors.at(id)) {
                max_succ = std::max(max_succ, compute_rank(s));
            }
        }
        rank_u[id] = w + max_succ;
        return rank_u[id];
    };
    for (auto const &j : jobs) compute_rank(j.first);


    std::vector<std::string> task_order;
    for (auto const &j : jobs) task_order.push_back(j.first);

    // Ordenação inicial (topológica aproximada)
    std::sort(task_order.begin(), task_order.end(), [&](const std::string &a, const std::string &b) {
        return rank_u[a] > rank_u[b];
    });

    std::vector<std::string> task_order_reverse = task_order;
    std::reverse(task_order_reverse.begin(), task_order_reverse.end());
    auto pct_table = compute_pct(comp_cost, successors, host_list, hosts_capabilities, task_order_reverse);

    
    std::map<std::string, double> aest, alst;
    compute_aest_alst(comp_cost, predecessors, successors, host_list, hosts_capabilities, aest, alst, task_order);

    std::set<std::string> critical_nodes;
    for (const auto& task : task_order) {
        if (alst.count(task) && aest.count(task) && alst.at(task) == aest.at(task)) {
            critical_nodes.insert(task);
        }
    }

    std::set<std::string> critical_parents;
    for (const auto& cn : critical_nodes) {
        if (predecessors.count(cn)) {
            for (const auto& p : predecessors.at(cn)) critical_parents.insert(p);
        }
    }

    auto cnct_table = compute_cnct(critical_nodes, successors, host_list, pct_table, task_order);

   
    std::map<std::string, double> rank_ipeft;
    for (const auto& task : task_order) {
        double sum_pct = 0.0;
        double avg_w_i = 0.0;
        int count = 0;
        for (const auto& host : host_list) {
            auto key = std::make_pair(task, host);
            if (pct_table.count(key)) sum_pct += pct_table.at(key);
            if (comp_cost.count(task) && hosts_capabilities.count(host) && comp_cost.at(task) > 0) {
                avg_w_i += comp_cost.at(task) / hosts_capabilities.at(host);
                count++;
            }
        }
        avg_w_i = (count > 0) ? avg_w_i / count : 0.0;
        rank_ipeft[task] = (host_list.empty() ? 0.0 : sum_pct / host_list.size()) + avg_w_i;
    }

    // Reordena task_order por rank_ipeft decrescente
    std::sort(task_order.begin(), task_order.end(), [&](const std::string &a, const std::string &b) {
        return rank_ipeft[a] > rank_ipeft[b];
    });

   
    std::map<std::string, std::string> scheduling_decision;
    std::map<std::string, double> task_finish_time;
    std::map<std::string, double> host_available_time;
    std::map<std::string, double> host_total_load;
    for (const auto& h : host_list) {
        host_available_time[h] = 0.0;
        host_total_load[h] = 0.0;
    }

    for (const auto& id : task_order) {
        double best_psi = std::numeric_limits<double>::max();
        std::string best_host = "";
        double ready_time = 0.0;
        if (predecessors.count(id)) {
            for (const auto& p : predecessors.at(id)) {
                if (task_finish_time.count(p)) {
                    ready_time = std::max(ready_time, task_finish_time.at(p));
                }
            }
        }

        double lambda = critical_parents.count(id) ? 0.0 : 1.0;

        for (const auto& h : hosts_capabilities) {
            std::string hostname = h.first;
            double flop_rate = h.second;
            double execution_time = (comp_cost.count(id) && comp_cost.at(id) > 0) ? comp_cost.at(id) / flop_rate : 0.001;
            double est = std::max(host_available_time[hostname], ready_time);
            double eft = est + execution_time;

            auto key = std::make_pair(id, hostname);
            double cnct = cnct_table.count(key) ? cnct_table.at(key) : 0.0;
            double psi = eft + lambda * cnct;

            if (psi < best_psi) {
                best_psi = psi;
                best_host = hostname;
            }
        }

        scheduling_decision[id] = best_host;
        task_finish_time[id] = best_psi - lambda * (cnct_table.count(std::make_pair(id, best_host)) ? cnct_table.at(std::make_pair(id, best_host)) : 0.0);
        host_available_time[best_host] = task_finish_time[id];
        if (comp_cost.count(id) && comp_cost.at(id) > 0) {
            host_total_load[best_host] += comp_cost.at(id) / hosts_capabilities[best_host];
        }
    }

    
    std::set<std::string> completed;
    std::set<std::string> submitted;
    std::map<std::string, std::string> actual_execution_host = scheduling_decision;

    while (completed.size() < jobs.size()) {
        for (const auto& id : task_order) {
            if (submitted.count(id)) continue;
            bool ready = true;
            if (predecessors.count(id)) {
                for (const auto& p : predecessors.at(id)) {
                    if (completed.find(p) == completed.end()) {
                        ready = false;
                        break;
                    }
                }
            }
            if (ready) {
                std::map<std::string, std::string> service_args;
                service_args[id] = scheduling_decision[id];
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

  
    std::string csv_filename = "resultados_" + this->getWorkflowName() + "_IPEFT.csv";
    std::ofstream csv_file(csv_filename);

    if (csv_file.is_open()) {
        csv_file << "task_id,host,finish_time,load_seconds\n";
        for (const auto& id : task_order) {
            double load = (comp_cost.count(id) && comp_cost.at(id) > 0 && actual_execution_host.count(id) && hosts_capabilities.count(actual_execution_host[id])) ?
                          comp_cost.at(id) / hosts_capabilities.at(actual_execution_host[id]) : 0.0;
            csv_file << id << ","
                     << actual_execution_host[id] << ","
                     << std::fixed << std::setprecision(4) << task_finish_time[id] << ","
                     << load << "\n";
        }
        csv_file.close();
        WRENCH_INFO("Log detalhado salvo em %s", csv_filename.c_str());
    }

    std::cout << "\n============================================================" << std::endl;
    std::cout << " RESUMO DE UTILIZAÇÃO DA PLATAFORMA (IPEFT)" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::left << std::setw(20) << "Host" << " | " << "Tempo Total de Uso (s)" << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;

    for (const auto& h : hosts_capabilities) {
        std::cout << std::left << std::setw(20) << h.first << " | "
                  << std::fixed << std::setprecision(4) << host_total_load[h.first] << " s" << std::endl;
    }

    std::cout << "------------------------------------------------------------" << std::endl;
    std::cout << "Makespan Total: " << std::fixed << std::setprecision(4) << (end_time - start_time) << " s" << std::endl;
    std::cout << "============================================================" << std::endl;

    return 0;
}

std::string Controller::getWorkflowName() {
    size_t last_slash = json_file_path.find_last_of("/\\");
    std::string name = (last_slash == std::string::npos) ? json_file_path : json_file_path.substr(last_slash + 1);
    size_t last_dot = name.find_last_of(".");
    if (last_dot != std::string::npos) name = name.substr(0, last_dot);
    return name;
}