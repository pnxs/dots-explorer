#pragma once
#include <dots/dots.h>
#include <models/MetadataModel.h>
#include <models/StructModel.h>
#include <widgets/StructListColumn.h>

struct ImGuiTableSortSpecs;

struct StructListRow
{
    enum MetaData { LastOp, LastPublished, LastPublishedBy, MetaDataSize };

    StructListRow(const StructDescriptorModel& structDescriptorModel, const dots::type::Struct& instance);
    StructListRow(const StructListRow& other) = delete;
    StructListRow(StructListRow&& other) = default;
    ~StructListRow() = default;

    StructListRow& operator = (const StructListRow& rhs) = delete;
    StructListRow& operator = (StructListRow&& rhs) = default;

    const char* widgetId() const;

    const MetadataModel& metadataModel() const;
    MetadataModel& metadataModel();

    const StructModel& structModel() const ;
    StructModel& structModel();

    bool less(const ImGuiTableSortSpecs& sortSpecs, const StructListRow& other) const;

    bool isSelected() const;
    void render();

private:

    inline static uint64_t M_nextWidgetId = 0;

    mutable std::string m_widgetId;
    MetadataModel m_metadataModel;
    StructModel m_structModel;
    std::vector<StructListColumn> m_columns;
};