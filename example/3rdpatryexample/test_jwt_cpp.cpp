#include <jwt-cpp/jwt.h>
#include <iostream>
int main(){ auto t = jwt::create().set_issuer("rcs").sign(jwt::algorithm::none{}); std::cout<<t<<"\n"; return 0; }
