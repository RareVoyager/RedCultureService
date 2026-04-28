#include <openssl/ssl.h>
#include <iostream>
int main(){ std::cout<<OpenSSL_version(OPENSSL_VERSION)<<"\n"; return 0; }
