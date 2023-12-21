#include "vdfstatemachine.h"

#include <sstream>

namespace VDFStateMachine {

    namespace APPID {
        void handleState(uint8_t& value, ParseState& state, FieldType& type) {
            if (value == 0x01) {
                state = ParseState::KEY;
                if (value == 0x01) type = FieldType::STRING;
            }
        }
    };

    namespace WAITING {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::vector<std::string>& listValue) {
            if (value != 0x08) {
                //Update the state
                state = ParseState::KEY;
                if (value == 0x01) type = FieldType::STRING;
                if (value == 0x02) type = FieldType::BOOLEAN;
                if (value == 0x00) {
                    listValue.clear();
                    type = FieldType::LIST;
                }
            }
        }
    };

    namespace KEY {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::ostringstream& utf8String, std::string& key) {
            if (value != 0x00) {
                utf8String << static_cast<char>(value);
            } else {
                //Set key and reset string
                key = utf8String.str();
                utf8String.str("");

                //Update the state
                state = ParseState::VALUE;
                if (key == "LastPlayTime") type = FieldType::DATE;
            }
        }
    }

    namespace VALUE {
        std::string delimit(const std::vector<std::string>& vec, char delimiter) {
            std::stringstream result;

            // Iterate through the vector
            for (auto it = vec.begin(); it != vec.end(); ++it) {
                // Append the current string to the result
                result << *it;

                // Add a comma if it's not the last element
                if (std::next(it) != vec.end()) {
                    result << delimiter;
                }
            }

            // Convert the stringstream to a string and return
            return result.str();
        }

        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::ostringstream& utf8String,
            std::string& key, std::map<std::string, std::string>& entry, ListParseState& listState, std::vector<std::string>& listValue,
            std::vector<char>& endingBuffer, std::vector<std::map<std::string, std::string>>& shortcuts) {
                  switch(type) {
                case FieldType::STRING:
                    if (value != 0x00) {
                        utf8String << static_cast<char>(value);
                    } else {
                        entry[key] = utf8String.str();
                        utf8String.str("");

                        //Update the state
                        state = ParseState::WAITING;
                    }
                    break;
                case FieldType::BOOLEAN:
                    if (value == 0x01) entry[key] = "true";
                    if (value == 0x00) entry[key] = "false";

                    //Update the state
                    state = ParseState::ENDING;
                    break;
                case FieldType::DATE:
                    entry[key] = "";

                    //Update the state
                    state = ParseState::ENDING;
                    break;
                case FieldType::LIST:
                    if (listState == ListParseState::WAITING && value == 0x08 && endingBuffer.size() < 1) {
                        endingBuffer.emplace_back(value);
                    } else if (listState == ListParseState::WAITING && value == 0x08 && endingBuffer.size() == 1) {
                        entry[key] = delimit(listValue, ',');
                        endingBuffer.clear();
                        //Update states
                        // Bit of a hack here but we know that tags is the only list and it's last off the block so we handle that here
                        listState = ListParseState::WAITING;
                        state = ParseState::APPID;
                        //Add this entry and make a new one
                        shortcuts.emplace_back(entry);
                        entry.clear();
                    } else if (listState == ListParseState::WAITING && value != 0x08) {
                        listState = ListParseState::INDEX;
                    } else if (listState == ListParseState::INDEX && value == 0x00) {
                        listState = ListParseState::VALUE;
                        utf8String.str("");
                    } else if (listState == ListParseState::VALUE && value != 0x00) {
                        utf8String << static_cast<char>(value);
                    } else if (listState == ListParseState::VALUE) {
                        listValue.emplace_back(utf8String.str());
                        listState = ListParseState::WAITING;
                        utf8String.str("");
                    }
                    break;
            }
        }

    }

    namespace ENDING {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::vector<char>& endingBuffer) {
            if ((type == FieldType::BOOLEAN || type == FieldType::DATE) && endingBuffer.size() < 2) {
                endingBuffer.emplace_back(value);
            } else if ((type == FieldType::BOOLEAN || type == FieldType::DATE) && endingBuffer.size() == 2) {
                endingBuffer.clear();
                //Update the state
                state = ParseState::WAITING;
            }
        }
    };
}
