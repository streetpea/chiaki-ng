#include "vdfstatemachine.h"

#include <QVariant>
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
        void handleState(uint8_t& value, ParseState& state, FieldType& type, QStringList& listValue) {
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
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::ostringstream& utf8String, QString& key) {
            if (value != 0x00) {
                utf8String << static_cast<char>(value);
            } else {
                //Set key and reset string
                key = QString::fromStdString(utf8String.str());
                utf8String.str("");

                //Update the state
                state = ParseState::VALUE;
                if (key == "LastPlayTime") type = FieldType::DATE;
            }
        }
    }

    namespace VALUE {
        void handleState(uint8_t& value, ParseState& state, FieldType& type, std::ostringstream& utf8String,
            QString& key, SteamShortcutEntry& entry, ListParseState& listState, QStringList& listValue,
            std::vector<char>& endingBuffer, QVector<SteamShortcutEntry>& shortcuts) {
                  switch(type) {
                case FieldType::STRING:
                    if (value != 0x00) {
                        utf8String << static_cast<char>(value);
                    } else {
                        entry.setProperty(key.toStdString().c_str(), QString::fromStdString(utf8String.str()));
                        utf8String.str("");

                        //Update the state
                        state = ParseState::WAITING;
                    }
                    break;
                case FieldType::BOOLEAN:
                    if (value == 0x01) entry.setProperty(key.toStdString().c_str(), "true");
                    if (value == 0x00) entry.setProperty(key.toStdString().c_str(), "false");

                    //Update the state
                    state = ParseState::ENDING;
                    break;
                case FieldType::DATE:
                    entry.setProperty(key.toStdString().c_str(), "");

                    //Update the state
                    state = ParseState::ENDING;
                    break;
                case FieldType::LIST:
                    if (listState == ListParseState::WAITING && value == 0x08 && endingBuffer.size() < 1) {
                        endingBuffer.emplace_back(value);
                    } else if (listState == ListParseState::WAITING && value == 0x08 && endingBuffer.size() == 1) {
                        entry.setProperty(key.toStdString().c_str(), listValue.join(','));
                        endingBuffer.clear();
                        //Update states
                        // Bit of a hack here but we know that tags is the only list and it's last off the block so we handle that here
                        listState = ListParseState::WAITING;
                        state = ParseState::APPID;
                        //Add this entry and make a new one
                        shortcuts.append(entry);
                        entry = SteamShortcutEntry();
                    } else if (listState == ListParseState::WAITING && value != 0x08) {
                        listState = ListParseState::INDEX;
                    } else if (listState == ListParseState::INDEX && value == 0x00) {
                        listState = ListParseState::VALUE;
                        utf8String.str("");
                    } else if (listState == ListParseState::VALUE && value != 0x00) {
                        utf8String << static_cast<char>(value);
                    } else if (listState == ListParseState::VALUE) {
                        listValue.append(QString::fromStdString(utf8String.str()));
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
