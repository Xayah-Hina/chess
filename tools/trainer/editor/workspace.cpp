module;
#include <Windows.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <shobjidl.h>
#include <wrl/client.h>
module tools.trainer.editor.workspace;
import tools.editor.style;
import std;
namespace tools::trainer::editor {
    Workspace::Workspace(tools::editor::WindowPlatform& platform) : window{platform}, session{[] { glfwPostEmptyEvent(); }} {
        tools::editor::apply_style();
        const auto text = config.run.u8string();
        std::copy(text.begin(), text.end(), directory.begin());
    }
    void Workspace::receive() {
        state = session.drain();
        if (state.loaded && loaded != state.config.run) {
            loaded = state.config.run;
            config = state.config;
            directory.fill(0);
            const auto text = config.run.u8string();
            std::copy(text.begin(), text.end(), directory.begin());
        }
    }
    void Workspace::draw(const bool closing) {
        const auto& viewport = *ImGui::GetMainViewport();
        const float scale    = ImGui::GetStyle().FontScaleDpi;
        ImGui::SetNextWindowPos(viewport.Pos);
        ImGui::SetNextWindowSize(viewport.Size);
        ImGui::Begin("##Trainer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        tools::editor::draw_window_controls(window, "训练面板  /  象棋 AI", scale);
        ImGui::SetCursorPosY(62 * scale);
        ImGui::PushFont(nullptr, 25);
        ImGui::TextUnformatted("训练工作台");
        ImGui::PopFont();
        ImGui::SameLine(0, 20 * scale);
        constexpr std::array phase_labels{"空闲", "加载中", "收集样本", "自我对弈", "更新模型", "正在暂停", "已暂停", "评估中", "保存中", "正在关闭", "错误"};
        ImGui::TextColored({0.70F, 0.68F, 1, 1}, "%s", phase_labels[int(state.phase)]);
        ImGui::TextDisabled("模型 v%llu  ·  累计训练 %.1f 分钟  ·  评估 %.1f 秒", state.version, state.training_seconds / 60, state.benchmark_seconds);
        ImGui::Spacing();
        const bool busy = state.training || state.active.has_value() || state.phase == Phase::loading || state.phase == Phase::saving || state.phase == Phase::pausing;
        ImGui::BeginDisabled(busy || closing);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 118 * scale);
        ImGui::InputText("##Directory", directory.data(), directory.size());
        ImGui::SameLine();
        if (ImGui::Button("选择目录", {108 * scale, 0})) {
            const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            if (FAILED(apartment)) throw std::runtime_error{std::format("Folder dialog COM: 0x{:08X}", unsigned(apartment))};
            struct Apartment final {
                ~Apartment() {
                    CoUninitialize();
                }
            } lifetime;
            Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
            HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()));
            if (SUCCEEDED(result)) result = dialog->SetOptions(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            if (SUCCEEDED(result)) result = dialog->SetTitle(L"选择训练目录");
            if (SUCCEEDED(result)) result = dialog->SetOkButtonLabel(L"选择");
            if (SUCCEEDED(result)) result = dialog->Show(window.native_window);
            if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                if (FAILED(result)) throw std::runtime_error{std::format("Folder dialog: 0x{:08X}", unsigned(result))};
                Microsoft::WRL::ComPtr<IShellItem> folder;
                result = dialog->GetResult(folder.GetAddressOf());
                PWSTR path{};
                if (SUCCEEDED(result)) result = folder->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (FAILED(result)) throw std::runtime_error{std::format("Selected directory: 0x{:08X}", unsigned(result))};
                const std::filesystem::path selected{path};
                CoTaskMemFree(path);
                directory.fill(0);
                const auto text = selected.u8string();
                std::copy(text.begin(), text.end(), directory.begin());
            }
        }
        config.run = std::filesystem::path{std::u8string{reinterpret_cast<const char8_t*>(directory.data())}};
        if (ImGui::Button("打开训练目录")) session.submit({Action::open, config});
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(closing || state.phase == Phase::loading || state.phase == Phase::saving || state.phase == Phase::pausing);
        if (!state.training) {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.29F, 0.27F, 0.54F, 1});
            if (ImGui::Button(state.loaded ? "继续训练" : "开始训练")) session.submit({Action::start, config});
            ImGui::PopStyleColor();
        } else if (ImGui::Button("暂停训练")) session.submit({Action::pause});
        ImGui::SameLine();
        ImGui::BeginDisabled(!state.loaded);
        if (ImGui::Button("保存")) session.submit({Action::save});
        ImGui::SameLine();
        if (ImGui::Button("短评估")) session.submit({Action::short_benchmark});
        ImGui::SameLine();
        if (ImGui::Button("深度评估")) session.submit({Action::deep_benchmark});
        if (state.active) {
            ImGui::SameLine();
            if (state.phase == Phase::paused) {
                if (ImGui::Button("继续评估")) session.submit({Action::resume_benchmark});
            } else if (ImGui::Button("暂停评估")) session.submit({Action::pause});
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (state.saved.time_since_epoch().count()) {
            const auto text = std::format("最近保存 {:%H:%M:%S}", std::chrono::current_zone()->to_local(state.saved));
            ImGui::TextDisabled("%s", text.c_str());
        }
        ImGui::Spacing();
        draw_training();
        if (ImGui::CollapsingHeader("训练配置")) {
            ImGui::BeginDisabled(state.loaded || closing);
            if (ImGui::BeginTable("##Settings", 3, ImGuiTableFlags_SizingStretchSame)) {
                ImGui::TableNextColumn();
                ImGui::InputInt("并行对局数", &config.training.actors);
                ImGui::InputInt("训练批量", &config.training.batch);
                ImGui::InputInt("每步搜索次数", &config.training.simulations);
                ImGui::TableNextColumn();
                ImGui::InputInt("CPU 线程数", &config.training.threads);
                ImGui::InputInt("候选着法数", &config.training.candidates);
                ImGui::InputFloat("学习率", &config.training.learning_rate, 0, 0, "%.6f");
                ImGui::TableNextColumn();
                ImGui::InputFloat("样本复用次数", &config.training.presentations);
                ImGui::InputInt("每组开局对数", &config.opening_pairs);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("每个开局交换红黑各下一局。默认每组 10 对，共 20 局；9 组对手合计 180 局。减少对局会扩大统计区间。");
                ImGui::InputInt("评估批量", &config.arena_batch);
                ImGui::EndTable();
            }
            ImGui::EndDisabled();
        }
        ImGui::Spacing();
        ImGui::SeparatorText("手动评估");
        ImGui::TextDisabled("仅在点击按钮后执行；评估期间暂停模型更新。");
        if (state.active) draw_benchmark(*state.active);
        for (const auto& request : state.queue) {
            ImGui::PushID(int(request.id));
            ImGui::Text("#%llu  %s  ·  排队中", request.id, request.deep ? "深度评估" : "短评估");
            ImGui::SameLine();
            if (ImGui::SmallButton("移除")) session.submit({Action::remove_benchmark, {}, request.id});
            ImGui::PopID();
        }
        if (state.latest) {
            ImGui::Spacing();
            ImGui::TextDisabled("最近完成的评估");
            draw_benchmark(*state.latest);
        } else if (!state.active && state.queue.empty()) ImGui::TextDisabled("还没有评估结果");
        if (!state.error.empty()) ImGui::TextColored({1, 0.55F, 0.50F, 1}, "%s", state.error.c_str());
        if (ImGui::CollapsingHeader("事件记录"))
            for (const auto& event : state.events) ImGui::TextUnformatted(event.c_str());
        ImGui::End();
    }
    void Workspace::draw_training() {
        if (ImGui::BeginTable("##Metrics", 4, ImGuiTableFlags_SizingStretchSame)) {
            const std::array labels{"优化更新", "完成对局", "回放样本", "未结束对局样本"};
            const std::array values{state.steps, state.games, state.replay, state.pending_samples};
            for (int index = 0; index < 4; ++index) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", labels[index]);
                ImGui::PushFont(nullptr, 23);
                ImGui::Text("%llu", values[index]);
                ImGui::PopFont();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("红胜 / 和棋 / 黑胜  %llu / %llu / %llu     %.1f 着/秒     %.2f 更新/秒", state.outcomes[0], state.outcomes[1], state.outcomes[2], state.plies_per_second, state.steps_per_second);
        if (!state.steps) {
            ImGui::TextDisabled("初始样本：%llu / %llu。对局结束后，样本才进入回放池。", state.generated, state.minimum_samples ? state.minimum_samples : std::uint64_t(std::ceil(config.training.batch / double(config.training.presentations))));
        }
        if (ImGui::BeginTable("##Charts", 2, ImGuiTableFlags_SizingStretchSame)) {
            const std::array labels{"策略 KL", "价值损失", "梯度范数", "参数更新幅度"};
            for (int index = 0; index < 4; ++index) {
                ImGui::TableNextColumn();
                std::vector<float> values;
                for (const auto& frame : state.history) values.push_back(frame.metrics[index]);
                const auto text = state.steps ? std::format("{}  {:.5f}", labels[index], state.metrics[index]) : std::format("{}  —", labels[index]);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PushID(index);
                ImGui::PlotLines("##Chart", values.data(), int(values.size()), 0, nullptr, FLT_MAX, FLT_MAX, {ImGui::GetContentRegionAvail().x, 66 * ImGui::GetStyle().FontScaleDpi});
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    void Workspace::draw_benchmark(const BenchmarkResult& result) {
        ImGui::PushID(int(result.request.id));
        ImGui::Text("#%llu  %s  ·  模型 v%llu  ·  %s  ·  计算 %.1f 秒", result.request.id, result.request.deep ? "深度评估" : "短战术评估", result.version, result.complete ? "已完成" : state.phase == Phase::paused ? "已暂停" : "进行中", result.seconds);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("模型 v%llu 保存于第 %llu 次优化更新；评估期间模型保持固定。", result.version, result.version);
        if (!result.complete) {
            ImGui::SameLine();
            if (ImGui::SmallButton("取消")) session.submit({Action::remove_benchmark, {}, result.request.id});
            ImGui::ProgressBar(float(result.probe_index) / result.probe_target, {-1, 0}, std::format("战术评估：{} / {} 组完成", result.probe_index, result.probe_target).c_str());
            if (!result.matches.empty()) {
                int completed{}, total{};
                for (const auto& match : result.matches) {
                    completed += match.completed_games;
                    total += match.target_pairs * 2;
                }
                ImGui::ProgressBar(float(completed) / total, {-1, 0}, std::format("对局评估：{} / {} 局完成", completed, total).c_str());
                ImGui::TextDisabled("战术评估完成后进行对局评估；进度条表示完成数量，不能用于推算剩余时间。");
            }
        }
        if (!result.complete && !result.stage.empty()) {
            std::string stage;
            if (result.stage.starts_with("Tactics: ")) stage = std::format("战术评估：{} 次搜索", result.stage.substr(9, result.stage.find(' ', 9) - 9));
            else if (result.stage == "Candidate search") stage = "模型搜索";
            else if (result.stage == "Reference search") stage = "参考模型搜索";
            else if (result.stage == "Opponent moves") stage = "对手落子";
            else if (result.stage == "Move adjudication") stage = "落子规则判定";
            else std::unreachable();
            if (result.activity.total) ImGui::TextDisabled("%s  ·  当前批次 %zu / %zu 完成  ·  %llu 次搜索", stage.c_str(), result.activity.completed, result.activity.total, result.activity.simulations);
            else ImGui::TextDisabled("%s", stage.c_str());
        }
        if (result.request.deep) {
            draw_matches(result);
            if (ImGui::CollapsingHeader("战术评估详情")) draw_probes(result);
        } else {
            ImGui::TextWrapped("检查模型能否找到胜着并避免立即失利；深度评估通过对弈比较模型与 Pikafish 各档位的棋力。");
            draw_probes(result);
        }
        ImGui::PopID();
    }
    void Workspace::draw_probes(const BenchmarkResult& result) {
        if (ImGui::BeginTable("##Probes", 6, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("决策方式");
            ImGui::TableSetupColumn("一着胜");
            ImGui::TableSetupColumn("三着胜");
            ImGui::TableSetupColumn("避败");
            ImGui::TableSetupColumn("正确动作概率");
            ImGui::TableSetupColumn("价值误差");
            ImGui::TableHeadersRow();
            for (int index = 0; index < result.probe_target; ++index) {
                const auto& probe = result.probes[index];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (!index) ImGui::TextUnformatted("直接使用网络");
                else ImGui::Text("%d 次搜索", result.request.deep ? index == 1 ? 32 : 128 : state.config.training.simulations);
                ImGui::TextDisabled("%llu 个局面", probe.positions);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("每个局面的 MCTS 搜索次数；评估期间模型保持固定。");
                for (int category = 0; category < 3; ++category) {
                    ImGui::TableNextColumn();
                    if (probe.total[category]) {
                        ImGui::Text("%.1f%%", 100.0 * probe.correct[category] / probe.total[category]);
                        ImGui::TextDisabled("%llu / %llu 正确", probe.correct[category], probe.total[category]);
                    } else ImGui::TextDisabled("待评估");
                }
                ImGui::TableNextColumn();
                if (probe.positions) {
                    ImGui::Text("%.1f%%", 100 * probe.mass / probe.positions);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("分配给全部正确着法的平均概率，越高越好。\n前三列表示答案正确率。\n直接使用网络时统计网络概率；搜索行统计搜索策略概率。");
                } else ImGui::TextDisabled("待评估");
                ImGui::TableNextColumn();
                if (probe.value_positions) {
                    ImGui::Text("%.4f", probe.brier / probe.value_positions);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("网络胜／和／负预测的 Brier 误差，越低越好。\n只统计已证明必胜的局面，不包含避败局面。\n完全正确为 0；三种结果各占 1/3 时约为 0.667。");
                    ImGui::TextDisabled("%llu 个必胜局面", probe.value_positions);
                } else ImGui::TextDisabled("等待必胜局面结果");
            }
            ImGui::EndTable();
        }
        if (!result.request.deep) ImGui::TextWrapped("网络评估使用完整题库，搜索评估使用少量子集；两行得分差不能直接衡量搜索带来的提升。");
        else ImGui::TextWrapped("三种方式使用相同的独立评估局面，可以比较不同搜索次数的效果。");
        if (ImGui::CollapsingHeader("指标说明")) {
            ImGui::TextWrapped("一着胜：找到立即获胜的着法。三着胜：找到三着以内的强制胜着（我方 → 对方 → 我方）。");
            ImGui::TextWrapped("避败：避免让对方下一着立即获胜，衡量模型能否避免直接送输。");
            ImGui::TextWrapped("前三列表示答案正确率；正确动作概率表示分配给正确着法的概率，均越高越好。价值误差越低越好，仅统计已证明必胜的局面。");
        }
    }
    void Workspace::draw_matches(const BenchmarkResult& result) {
        constexpr ImVec4 neutral{0.68F, 0.69F, 0.75F, 1}, positive{0.49F, 0.82F, 0.66F, 1}, uncertain{0.94F, 0.78F, 0.46F, 1}, negative{0.95F, 0.56F, 0.52F, 1};
        std::vector<std::pair<const char*, ImVec4>> conclusions;
        const MatchResult *passed{}, *unresolved{}, *below{};
        int finished{}, total{};
        for (const auto& match : result.matches) {
            conclusions.emplace_back("进行中", neutral);
            if (match.pikafish_nodes) ++total;
            if (!match.complete) continue;
            if (match.pikafish_nodes) ++finished;
            if (match.statistics.lower > 0.5) {
                conclusions.back() = {"更强", positive};
                if (match.pikafish_nodes && (!passed || match.pikafish_nodes > passed->pikafish_nodes)) passed = &match;
            } else if (match.statistics.upper < 0.5) {
                conclusions.back() = {"更弱", negative};
                if (match.pikafish_nodes && (!below || match.pikafish_nodes < below->pikafish_nodes)) below = &match;
            } else {
                conclusions.back() = {"尚无定论", uncertain};
                if (match.pikafish_nodes && (!unresolved || match.pikafish_nodes > unresolved->pikafish_nodes)) unresolved = &match;
            }
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("棋力参考");
        if (!finished) ImGui::TextColored(neutral, "尚未评定棋力：还没有 Pikafish 档位完成全部对局。");
        if (passed) ImGui::TextColored(positive, "有证据表明强于：%s", passed->name.c_str());
        if (unresolved) ImGui::TextColored(uncertain, "对阵 %s 尚无定论（区间包含 50%%）", unresolved->name.c_str());
        if (below) ImGui::TextColored(negative, "有证据表明弱于：%s", below->name.c_str());
        ImGui::TextDisabled("Pikafish：%d / %d 档位完成；未完成档位不计入结论。", finished, total);
        ImGui::TextWrapped("PF-N2000 表示 Pikafish 每着约搜索 2,000 个节点；结论仅适用于本次对弈条件，不能换算为人类段位。");
        if (ImGui::CollapsingHeader("对局结果", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("##Matches", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("对手 / 决策方式");
            ImGui::TableSetupColumn("进度");
            ImGui::TableSetupColumn("胜 / 和 / 负");
            ImGui::TableSetupColumn("得分 / 95% 区间");
            ImGui::TableSetupColumn("结论");
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < result.matches.size(); ++index) {
                const auto& match = result.matches[index];
                const auto& value = match.statistics;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(match.name.c_str());
                if (match.simulations) ImGui::TextDisabled("我方搜索：%d 次", match.simulations);
                else ImGui::TextDisabled("我方直接使用网络");
                ImGui::TableNextColumn();
                ImGui::Text("%d / %d 局", match.completed_games, match.target_pairs * 2);
                ImGui::TextDisabled("已走 %llu 着  ·  %llu 局计入得分", match.plies, value.pairs * 2);
                ImGui::TableNextColumn();
                ImGui::Text("%llu / %llu / %llu", value.wdl[0], value.wdl[1], value.wdl[2]);
                ImGui::TableNextColumn();
                if (value.pairs) {
                    ImGui::Text("%.1f%%", value.score * 100);
                    ImGui::TextDisabled("%.1f%% - %.1f%%", value.lower * 100, value.upper * 100);
                    if (value.score > 0 && value.score < 1) {
                        ImGui::TextDisabled("相对 Elo %+.0f", 400 * std::log10(value.score / (1 - value.score)));
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("相对于该对手的 Elo 差值：正数表示更强，负数表示更弱；仅适用于本次对弈条件。");
                    } else ImGui::TextDisabled("%s：无法给出有限的 Elo 估计", value.score == 1 ? "全胜" : "全负");
                } else ImGui::TextDisabled("等待成对对局完成");
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, conclusions[index].second);
                ImGui::TextWrapped("%s", conclusions[index].first);
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::TextWrapped("得分 =（胜局 + 和局 × 0.5）/ 局数。每个开局交换红黑各下一局；只有两局均完成才计入得分。");
        ImGui::TextWrapped("95%% 区间全部高于 50%%，支持我方更强；全部低于 50%%，支持我方更弱；包含 50%% 则尚无定论。须完成该对手的全部对局才给出结论。");
    }
} // namespace tools::trainer::editor
