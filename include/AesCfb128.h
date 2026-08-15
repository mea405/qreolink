#pragma once

#include <QByteArray>
#include <QString>

// AES-128-CFB (128-bit segment) with zero-padding, matching CryptoJS defaults
// used by Reolink web CGI encryption.
namespace AesCfb128 {

[[nodiscard]] QByteArray encrypt(const QByteArray& key16,
                                 const QByteArray& iv16,
                                 const QByteArray& plaintext);

[[nodiscard]] QByteArray decrypt(const QByteArray& key16,
                                 const QByteArray& iv16,
                                 const QByteArray& ciphertext);

[[nodiscard]] QString encryptToBase64(const QString& key16,
                                      const QString& iv16,
                                      const QString& plaintextUtf8);

[[nodiscard]] QString decryptFromBase64(const QString& key16,
                                        const QString& iv16,
                                        const QString& ciphertextBase64);

}  // namespace AesCfb128
