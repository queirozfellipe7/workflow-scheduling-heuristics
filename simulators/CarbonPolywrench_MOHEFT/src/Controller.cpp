#include "Controller.h"
#include <fstream>
#include <limits>
#include <algorithm>
#include <functional>
#include <iomanip>
#include "json.hpp"

WRENCH_LOG_CATEGORY(controller, "Log category for Controller");

using json = nlohmann::json;

// Parâmetros MOHEFT
const int MOHEFT_K = 8;               
const double CARBON_INTENSITY = 450.0; 


bool Controller::dominates(const Solution& a, const Solution& b) const {
    bool better_makespan = a.makespan <= b.makespan;
    bool better_energy   = a.energy_joules <= b.energy_joules;
    bool better_carbon   = a.carbon_gco2   <= b.carbon_gco2;

    bool strictly_better = (a.makespan < b.makespan) ||
                           (a.energy_joules < b.energy_joules) ||
                           (a.carbon_gco2 < b.carbon_gco2);

    return better_makespan && better_energy && better_carbon && strictly_better;
}


Controller::Solution Controller::select_best_solution(const std::vector<Solution>& pareto_front) const {
    if (pareto_front.empty()) return Solution{};

    Solution best = pareto_front[0];
    double best_score = std::numeric_limits<double>::max();

    for (const auto& s : pareto_front) {
        // pesos exemplificativos – podem ser ajustados
        double score = 0.50 * s.makespan +
                       0.30 * (s.energy_joules / 1e6) +   // MJ
                       0.20 * (s.carbon_gco2 / 1000.0);   // kgCO₂
        if (score < best_score) {
            best_score = score;
            best = s;
        }
    }
    return best;
}

Controller::Controller(
    const std::shared_ptr<wrench::BareMetalComputeService> &bare_metal_compute_service,
    const std::shared_ptr<wrench::SimpleStorageService> &storage_service,
    const std::string &hostname,
    const std::string &json_file_path
) : wrench::ExecutionController(hostname, "controller"),
    bare_metal_compute_service(bare_metal_compute_service),
    storage_service(storage_service),
    json_file_path(json_file_path) {}

int Controller::main() {
    wrench::TerminalOutput::setThisProcessLoggingColor(wrench::TerminalOutput::COLOR_GREEN);
    WRENCH_INFO("Controller MOHEFT iniciando (K=%d)", MOHEFT_K);

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

    std::map<std::string, double> host_pmax, host_pidle;
    for (const auto& h : host_list) {
        if (h == "DGX_System") {
            host_pmax[h]  = 3300.0;
            host_pidle[h] = 1100.0;
        } else if (h.find("GPU") != std::string::npos) {
            bool high_power = (h == "GPU1" || h == "GPU2" || h == "GPU3" || h == "GPU4");
            host_pmax[h]  = high_power ? 400.0 : 250.0;
            host_pidle[h] = high_power ? 80.0  : 50.0;
        }
    }

    
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

    
    std::vector<Solution> front = {Solution{}};
    for (const auto& task : task_order) {
        if (comp_cost[task] == 0.0) continue;  // Pular tarefas I/O (não computacionais)

        std::vector<Solution> candidates;

        for (const auto& curr : front) {
            double ready_time = 0.0;
            for (const auto& p : predecessors[task]) {
                auto it = curr.finish_time.find(p);
                if (it != curr.finish_time.end()) {
                    ready_time = std::max(ready_time, it->second);
                }
            }

            for (const auto& hostname : host_list) {
                double flop_rate = hosts_capabilities[hostname];
                double execution_time = (comp_cost[task] > 0) ? comp_cost[task] / flop_rate : 0.001;

                double avail = 0.0;
                auto it_host = curr.host_avail.find(hostname);
                if (it_host != curr.host_avail.end()) {
                    avail = it_host->second;
                }

                double est = std::max(avail, ready_time);
                double eft = est + execution_time;

                double energy = host_pmax[hostname] * execution_time;  // Joules

                Solution news = curr;
                news.assignment[task] = hostname;
                news.finish_time[task] = eft;
                news.host_avail[hostname] = eft;
                news.makespan = std::max(news.makespan, eft);
                news.energy_joules += energy;
                news.carbon_gco2 += (energy / 3.6e6) * CARBON_INTENSITY;

                candidates.push_back(news);
            }
        }

       
        std::vector<Solution> new_front;
        for (auto& cand : candidates) {
            bool dominated = false;
            for (auto it = new_front.begin(); it != new_front.end(); ) {
                if (dominates(*it, cand)) {
                    dominated = true;
                    break;
                }
                if (dominates(cand, *it)) {
                    it = new_front.erase(it);
                } else {
                    ++it;
                }
            }
            if (!dominated) {
                new_front.push_back(cand);
            }
        }

        
        if (new_front.size() > static_cast<size_t>(MOHEFT_K)) {
            std::sort(new_front.begin(), new_front.end(),
                      [](const Solution& a, const Solution& b) { return a.makespan < b.makespan; });
            new_front.resize(MOHEFT_K);
        }

        front = std::move(new_front);
    }

    
    Solution best = select_best_solution(front);

    
    std::set<std::string> completed;
    std::set<std::string> submitted;
    std::map<std::string, std::string> actual_execution_host = best.assignment;

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
                service_args[id] = best.assignment[id];
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

    
    std::string csv_filename = "resultados_" + this->getWorkflowName() + "_MOHEFT.csv";
    std::ofstream csv_file(csv_filename);
   
    if (csv_file.is_open()) {
        csv_file << "task_id,host,finish_time,load_seconds\n";
        for (auto const &id : task_order) {
            auto it_finish = best.finish_time.find(id);
            double finish = (it_finish != best.finish_time.end()) ? it_finish->second : 0.0;
            csv_file << id << ","
                     << actual_execution_host[id] << ","
                     << std::fixed << std::setprecision(4) << finish << ","
                     << ((comp_cost[id] > 0) ? comp_cost[id] / hosts_capabilities[actual_execution_host[id]] : 0.0) << "\n";
        }
        csv_file.close();
        WRENCH_INFO("Log detalhado salvo em %s", csv_filename.c_str());
    }
    // 9.2. Exibe no terminal APENAS o resumo de uso e Makespan
    std::cout << "\n============================================================" << std::endl;
    std::cout << " RESUMO DE UTILIZAÇÃO DA PLATAFORMA" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::left << std::setw(20) << "Host" << " | " << "Tempo Total de Uso (s)" << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;
   
    std::map<std::string, double> host_total_load;
    for (const auto& h : host_list) host_total_load[h] = 0.0;

    for (const auto& id : task_order) {
        if (comp_cost[id] > 0) {
            std::string host = actual_execution_host[id];
            host_total_load[host] += comp_cost[id] / hosts_capabilities[host];
        }
    }

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