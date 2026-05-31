import modern.log.core;

#include <span>
#include <type_traits>

int main() {
    using namespace modern::log;

    static_assert(std::is_enum_v<level>);
    static_assert(std::is_enum_v<field_type>);
    static_assert(std::is_standard_layout_v<byte_view>);
    static_assert(std::is_standard_layout_v<field>);
    static_assert(std::is_standard_layout_v<record>);
    static_assert(std::is_standard_layout_v<batch_view>);

    field_value value{};
    value.type = field_type::unsigned_integer;
    value.storage.unsigned_integer = 42;

    field fields[1]{};
    fields[0].name = "answer";
    fields[0].value = value;

    record records[1]{};
    records[0].event_name = "bootstrap.schema";
    records[0].message_template = "contract smoke";
    records[0].fields = std::span<const field>(fields, 1);

    batch_view batch{records, 1};

    if (batch.empty()) {
        return 1;
    }

    return batch.data[0].fields.size() == 1 ? 0 : 1;
}