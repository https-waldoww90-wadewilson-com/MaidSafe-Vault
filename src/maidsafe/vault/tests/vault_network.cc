/*  Copyright 2012 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#include "maidsafe/vault/tests/vault_network.h"

#ifndef MAIDSAFE_WIN32
#include <ulimit.h>
#endif

#include <algorithm>
#include <string>

#include "maidsafe/common/test.h"
#include "maidsafe/common/log.h"
#include "maidsafe/common/visualiser_log.h"
#include "maidsafe/passport/detail/fob.h"

#include "maidsafe/vault_manager/vault_config.h"

#include "maidsafe/vault/tests/tests_utils.h"

namespace maidsafe {

namespace vault {

namespace test {

std::shared_ptr<VaultNetwork> VaultEnvironment::g_env_ = std::shared_ptr<VaultNetwork>();

VaultNetwork::VaultNetwork()
    : vaults_(), clients_(), public_pmids_(), bootstrap_contacts_(),
      vault_dir_(fs::unique_path((fs::temp_directory_path())))
#ifndef MAIDSAFE_WIN32
      , kUlimitFileSize([]()->long {  // NOLINT
                          long current_size(ulimit(UL_GETFSIZE));  // NOLINT
                          if (current_size < kLimitsFiles)
                            ulimit(UL_SETFSIZE, kLimitsFiles);
                          return current_size;
                        }())
#endif
{
  routing::Parameters::append_local_live_port_endpoint = true;
}

void VaultNetwork::SetUp() {
  for (size_t index(0); index < kNetworkSize; ++index)
    EXPECT_TRUE(AddVault());
}

void VaultNetwork::TearDown() {
  LOG(kInfo) << "VaultNetwork TearDown";
  for (auto& client : clients_)
    client->Stop();
  Sleep(std::chrono::seconds(1));
  for (auto& client : clients_)
    client.reset();
  Sleep(std::chrono::seconds(1));
  clients_.clear();
  for (auto& vault : vaults_)
    vault->Stop();
  Sleep(std::chrono::seconds(1));
  for (auto& vault : vaults_)
    vault.reset();
  vaults_.clear();

#ifndef MAIDSAFE_WIN32
  ulimit(UL_SETFSIZE, kUlimitFileSize);
#endif
}

bool VaultNetwork::Create(const passport::detail::Fob<passport::detail::PmidTag>& pmid) {
  std::string path_str("vault" + RandomAlphaNumericString(6));
  auto vault_root_dir(vault_dir_ / path_str);
  fs::create_directory(vault_root_dir);
  try {
    LOG(kVerbose) << "vault joining: " << vaults_.size() << " id: "
                  << DebugId(NodeId(pmid.name()->string()));
    vault_manager::VaultConfig vault_config(pmid, vault_root_dir, DiskUsage(1000000000),
                                            bootstrap_contacts_);
    vaults_.emplace_back(new Vault(vault_config, [](const boost::asio::ip::udp::endpoint&) {}));
    LOG(kSuccess) << "vault joined: " << vaults_.size() << " id: "
                  << DebugId(NodeId(pmid.name()->string()));
    public_pmids_.push_back(passport::PublicPmid(pmid));
    return true;
  }
  catch (const std::exception& ex) {
    LOG(kError) << "vault failed to join: " << vaults_.size() << " because: "
                << boost::diagnostic_information(ex);
    return false;
  }
  return false;
}

bool VaultNetwork::AddVault() {
  passport::PmidAndSigner pmid_and_signer(passport::CreatePmidAndSigner());
  auto future(clients_.front()->Put(passport::PublicPmid(pmid_and_signer.first)));
  try {
    future.get();
  }
  catch (const std::exception& error) {
    LOG(kVerbose) << "Failed to store pmid " << error.what();
    return false;
  }
  return Create(pmid_and_signer.first);
}

void VaultNetwork::AddClient() {
  passport::MaidAndSigner maid_and_signer{ passport::CreateMaidAndSigner() };
  AddClient(maid_and_signer, bootstrap_contacts_);
}

void VaultNetwork::AddClient(const passport::Maid& maid,
                             const routing::BootstrapContacts& bootstrap_contacts) {
  clients_.emplace_back(nfs_client::MaidNodeNfs::MakeShared(maid, bootstrap_contacts));
}

void VaultNetwork::AddClient(const passport::MaidAndSigner& maid_and_signer,
                             const routing::BootstrapContacts& bootstrap_contacts) {

  clients_.emplace_back(nfs_client::MaidNodeNfs::MakeShared(maid_and_signer, bootstrap_contacts));
}

}  // namespace test

}  // namespace vault

}  // namespace maidsafe

