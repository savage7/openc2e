#include "GeneInspector.h"

#include "ImGuiUtils.h"
#include "World.h"
#include "creatures/CreatureAgent.h"
#include "creatures/c2eCreature.h"
#include "fileformats/genomeFile.h"

#include <fmt/core.h>
#include <imgui.h>

namespace Openc2eImgui {

static bool s_gene_inspector_open = false;

void SetGeneInspectorOpen(bool value) {
    s_gene_inspector_open = value;
}

static const char* stageName(lifestage s) {
    switch (s) {
        case baby:       return "Baby";
        case child:      return "Child";
        case adolescent: return "Adolescent";
        case youth:      return "Youth";
        case adult:      return "Adult";
        case old:        return "Old";
        case senile:     return "Senile";
        default:         return "?";
    }
}

void DrawGeneInspector() {
    if (!ImGuiUtils::BeginWindow("Gene Inspector", &s_gene_inspector_open)) {
        return;
    }

    CreatureAgent* ca = dynamic_cast<CreatureAgent*>(world.selectedcreature.get());
    Creature* creature = ca ? ca->getCreature() : nullptr;

    if (!creature) {
        ImGui::TextUnformatted("No creature selected.");
        ImGui::End();
        return;
    }

    std::shared_ptr<genomeFile> genome = creature->getGenome();
    if (!genome) {
        ImGui::TextUnformatted("No genome loaded.");
        ImGui::End();
        return;
    }

    ImGui::Text("Genes: %zu", genome->genes.size());
    ImGui::Separator();

    ImGuiTableFlags table_flags =
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_SizingStretchProp;

    ImVec2 table_size(0.0f, ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginTable("genes", 6, table_flags, table_size)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#",     ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Gen",   ImGuiTableColumnFlags_WidthFixed, 35.0f);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < genome->genes.size(); ++i) {
            gene* g = genome->genes[i].get();
            if (!g) continue;

            ImGui::TableNextRow();

            // Column 0: index
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", i);

            // Column 1: type name (e.g. "Brain", "Biochemistry")
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(g->typeName() ? g->typeName() : "?");

            // Column 2: gene name (e.g. "Lobe", "Receptor")
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(g->name() ? g->name() : "?");

            // Column 3: switch-on life stage
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(stageName(g->header.switchontime));

            // Column 4: generation
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", (int)g->header.generation);

            // Column 5: flags — M=mutable, D=dupable, C=delable/cutable, R=notexpressed/dormant
            ImGui::TableSetColumnIndex(5);
            char flags_buf[5] = {
                g->header.flags._mutable    ? 'M' : '-',
                g->header.flags.dupable     ? 'D' : '-',
                g->header.flags.delable     ? 'C' : '-',
                g->header.flags.notexpressed? 'R' : '-',
                '\0'
            };
            ImGui::TextUnformatted(flags_buf);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace Openc2eImgui
