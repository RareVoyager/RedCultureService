#include <nlohmann/json.hpp>
#include <iostream>
int main(){ nlohmann::json j={{"ok",true}}; std::cout<<j.dump()<<"\n"; return 0; }
