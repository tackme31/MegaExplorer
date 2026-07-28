#pragma once
#include "core/ISessionStore.h"

#include <gmock/gmock.h>

class MockSessionStore : public ISessionStore
{
public:
    MOCK_METHOD(Result<std::string>, loadSession, (), (const, override));
    MOCK_METHOD(Result<void>, saveSession, (const std::string&), (override));
    MOCK_METHOD(Result<void>, clearSession, (), (override));
};
