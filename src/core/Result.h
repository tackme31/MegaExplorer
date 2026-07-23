#pragma once
#include <string>

template <typename T>
struct Result
{
    bool success = false;
    T value{};
    std::string errorMessage;
    int errorCode = 0;

    static Result<T> ok(T v) { Result<T> r; r.success = true; r.value = std::move(v); return r; }
    static Result<T> fail(std::string message, int code = -1)
    {
        Result<T> r; r.success = false; r.errorMessage = std::move(message); r.errorCode = code; return r;
    }
};

// void has no value member, so this needs its own specialization.
template <>
struct Result<void>
{
    bool success = false;
    std::string errorMessage;
    int errorCode = 0;

    static Result<void> ok() { Result<void> r; r.success = true; return r; }
    static Result<void> fail(std::string message, int code = -1)
    {
        Result<void> r; r.success = false; r.errorMessage = std::move(message); r.errorCode = code; return r;
    }
};
