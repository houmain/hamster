#pragma once

#include <asio/ssl.hpp>
#include <filesystem>
#include <map>
#include <memory>
#include <openssl/x509v3.h>

class CertificateAuthority {
public:
  explicit CertificateAuthority(const std::filesystem::path& directory);
  std::shared_ptr<asio::ssl::context> context(const std::string& host);
  std::string public_certificate_pem() const;

private:
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> m_key{ nullptr, EVP_PKEY_free };
  std::unique_ptr<X509, decltype(&X509_free)> m_certificate{ nullptr, X509_free };
  std::map<std::string, std::shared_ptr<asio::ssl::context>> m_cache;
};
