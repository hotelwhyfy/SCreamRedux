#pragma once

class Engine;

namespace api {

enum class Status { Done, Partial, Stub };

struct Info {
    const char* name;
    Status status;
    const char* note;
};

// Register every sc* function the original scream.exe exports.
void registerAll(Engine& engine);

// The full table, for --api-status.
const Info* table(int& count);

}  // namespace api
