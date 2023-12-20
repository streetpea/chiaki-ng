#ifndef VDFSTATEMACHINE_H
#define VDFSTATEMACHINE_H

#include <vector>
#include <map>

namespace VDFStateMachine {

    enum class FieldType {
        STRING,
        BOOLEAN,
        LIST,
        DATE
    };

    enum class ParseState {
        APPID,
        WAITING,
        KEY,
        VALUE,
        ENDING
    };

    enum class ListParseState {
        WAITING,
        INDEX,
        VALUE,
        ENDING
    };

    namespace APPID {
        void handleState(uint8_t& value, ParseState& state, FieldType& type);
    };

    namespace WAITING {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::vector<std::string>& listValue);
    };

    namespace KEY {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::ostringstream& utf8String, std::string& key);
    }

    namespace VALUE {
        std::string delimit(const std::vector<std::string>& vec, char delimiter);

        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::ostringstream& utf8String,
            std::string& key, std::map<std::string, std::string>& entry, ListParseState& listState, std::vector<std::string>& listValue,
            std::vector<char>& endingBuffer, std::vector<std::map<std::string, std::string>>& shortcuts);
    }

    namespace ENDING {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::vector<char>& endingBuffer);
    };
}

#endif // VDFSTATEMACHINE_H
