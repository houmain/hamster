#include "CertificateAuthority.h"
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <fstream>
#include <array>
#include <stdexcept>

namespace {
  void check(bool success) {
    if (!success)
      throw std::runtime_error("Certificate generation or loading failed");
  }

  auto make_key() {
    auto ctx = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>(
      EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
    check(ctx && EVP_PKEY_keygen_init(ctx.get()) == 1);
    check(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 2048) == 1);
    auto* result = static_cast<EVP_PKEY*>(nullptr);
    check(EVP_PKEY_keygen(ctx.get(), &result) == 1);
    return std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(result, EVP_PKEY_free);
  }

  void extension(X509* cert, X509* issuer, int nid, const std::string& value) {
    auto ctx = X509V3_CTX{};
    X509V3_set_ctx(&ctx, issuer, cert, nullptr, nullptr, 0);
    auto ext = std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)>(
      X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str()), X509_EXTENSION_free);
    check(ext && X509_add_ext(cert, ext.get(), -1) == 1);
  }

  auto make_certificate(
    EVP_PKEY* key, const std::string& name, X509* issuer, EVP_PKEY* signing_key) {
    auto cert = std::unique_ptr<X509, decltype(&X509_free)>(X509_new(), X509_free);
    check(bool(cert));
    check(X509_set_version(cert.get(), 2) == 1);
    auto serial = std::array<unsigned char, 20>{};
    check(RAND_bytes(serial.data(), static_cast<int>(serial.size())) == 1);
    serial[0] &= 0x7f;
    serial[0] |= 1;
    auto bn = std::unique_ptr<BIGNUM, decltype(&BN_free)>(
      BN_bin2bn(serial.data(), static_cast<int>(serial.size()), nullptr), BN_free);
    check(bn && BN_to_ASN1_INTEGER(bn.get(), X509_get_serialNumber(cert.get())));
    check(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -300) != nullptr);
    check(
      X509_gmtime_adj(X509_getm_notAfter(cert.get()), (issuer ? 30L : 3650L) * 86400) != nullptr);
    check(X509_set_pubkey(cert.get(), key) == 1);
    auto subject =
      std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)>(X509_NAME_new(), X509_NAME_free);
    const auto common_name = name.size() <= 64 ? name : "Hamster intercepted host";
    check(
      subject && X509_NAME_add_entry_by_txt(subject.get(), "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0) == 1);
    check(X509_set_subject_name(cert.get(), subject.get()) == 1);
    check(X509_set_issuer_name(
      cert.get(), issuer ? X509_get_subject_name(issuer) : subject.get()) == 1);
    extension(cert.get(), issuer ? issuer : cert.get(), NID_basic_constraints,
      issuer ? "critical,CA:FALSE" : "critical,CA:TRUE,pathlen:0");
    extension(cert.get(), issuer ? issuer : cert.get(), NID_key_usage,
      issuer ? "critical,digitalSignature,keyEncipherment" : "critical,keyCertSign,cRLSign");
    extension(cert.get(), issuer ? issuer : cert.get(), NID_subject_key_identifier, "hash");
    if (issuer)
      extension(cert.get(), issuer, NID_authority_key_identifier, "keyid:always");
    if (issuer) {
      auto ec = asio::error_code{};
      asio::ip::make_address(name, ec);
      extension(cert.get(), issuer, NID_subject_alt_name, (ec ? "DNS:" : "IP:") + name);
      extension(cert.get(), issuer, NID_ext_key_usage, "serverAuth");
    }
    check(X509_sign(cert.get(), signing_key, EVP_sha256()) > 0);
    return cert;
  }

  auto read_pem(const std::filesystem::path& path) {
    auto in = std::ifstream(path, std::ios::binary);
    if (!in)
      throw std::runtime_error("Cannot read CA file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), {});
  }

  void write_pem(const std::filesystem::path& path, BIO* bio) {
    auto* data = static_cast<char*>(nullptr);
    auto size = BIO_get_mem_data(bio, &data);
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    out.write(data, size);
    out.close();
    if (!out)
      throw std::runtime_error("Cannot write CA file: " + path.string());
  }
} // namespace

CertificateAuthority::CertificateAuthority(const std::filesystem::path& directory) {
  const auto cert_path = directory / "ca.crt";
  const auto key_path = directory / "ca.key";
  if (std::filesystem::exists(cert_path) || std::filesystem::exists(key_path)) {
    auto cert = read_pem(cert_path), key = read_pem(key_path);
    auto cb = std::unique_ptr<BIO, decltype(&BIO_free)>(
      BIO_new_mem_buf(cert.data(), static_cast<int>(cert.size())), BIO_free);
    auto kb = std::unique_ptr<BIO, decltype(&BIO_free)>(
      BIO_new_mem_buf(key.data(), static_cast<int>(key.size())), BIO_free);
    m_certificate.reset(PEM_read_bio_X509(cb.get(), nullptr, nullptr, nullptr));
    m_key.reset(PEM_read_bio_PrivateKey(kb.get(), nullptr, nullptr, nullptr));
    check(m_certificate && m_key && X509_check_private_key(m_certificate.get(), m_key.get()) == 1);
    check(X509_check_ca(m_certificate.get()) > 0);
    check(X509_cmp_current_time(X509_get0_notBefore(m_certificate.get())) < 0 &&
      X509_cmp_current_time(X509_get0_notAfter(m_certificate.get())) > 0);
  }
  else {
    std::filesystem::create_directories(directory);
#ifndef _WIN32
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
#endif
    m_key = make_key();
    m_certificate = make_certificate(m_key.get(), "Hamster Local CA", nullptr, m_key.get());
    auto bio = std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_s_mem()), BIO_free);
    check(bio && PEM_write_bio_PrivateKey(
      bio.get(), m_key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1);
    write_pem(key_path, bio.get());
#ifndef _WIN32
    std::filesystem::permissions(
      key_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
#endif
    check(BIO_reset(bio.get()) == 1);
    check(PEM_write_bio_X509(bio.get(), m_certificate.get()) == 1);
    write_pem(cert_path, bio.get());
  }
}

std::shared_ptr<asio::ssl::context> CertificateAuthority::context(const std::string& host) {
  auto found = m_cache.find(host);
  if (found != m_cache.end())
    return found->second;
  auto key = make_key();
  auto cert = make_certificate(key.get(), host, m_certificate.get(), m_key.get());
  auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
  check(SSL_CTX_set_min_proto_version(ctx->native_handle(), TLS1_2_VERSION) == 1);
  check(SSL_CTX_use_certificate(ctx->native_handle(), cert.get()) == 1);
  check(SSL_CTX_use_PrivateKey(ctx->native_handle(), key.get()) == 1);
  // Supply the issuer as well as the leaf so clients can build the chain
  // directly. It is still accepted only if the client independently trusts it.
  check(SSL_CTX_add1_chain_cert(ctx->native_handle(), m_certificate.get()) == 1);
  // Bound memory use; live TLS connections retain their own context references.
  if (m_cache.size() >= 256)
    m_cache.erase(m_cache.begin());
  m_cache[host] = ctx;
  return ctx;
}

std::string CertificateAuthority::public_certificate_pem() const {
  auto buffer = std::unique_ptr<BIO, decltype(&BIO_free)>(BIO_new(BIO_s_mem()), BIO_free);
  check(buffer && PEM_write_bio_X509(buffer.get(), m_certificate.get()) == 1);
  auto* data = static_cast<char*>(nullptr);
  const auto size = BIO_get_mem_data(buffer.get(), &data);
  check(size > 0);
  return std::string(data, static_cast<size_t>(size));
}
