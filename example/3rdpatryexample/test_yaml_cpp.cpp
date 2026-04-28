#include <yaml-cpp/yaml.h>
#include <iostream>
int main(){ YAML::Node n; n["k"]="v"; std::cout<<n["k"].as<std::string>()<<"\n"; return 0; }
