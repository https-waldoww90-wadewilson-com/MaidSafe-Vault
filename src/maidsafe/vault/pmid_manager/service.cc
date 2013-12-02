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

#include "maidsafe/vault/pmid_manager/service.h"

#include "maidsafe/common/error.h"

#include "maidsafe/data_types/data_name_variant.h"

#include "maidsafe/vault/pmid_manager/pmid_manager.pb.h"
#include "maidsafe/vault/pmid_manager/metadata.h"
#include "maidsafe/vault/operation_handlers.h"
#include "maidsafe/vault/pmid_manager/value.h"
#include "maidsafe/vault/pmid_manager/pmid_manager.h"
#include "maidsafe/vault/sync.pb.h"

namespace fs = boost::filesystem;

namespace maidsafe {
namespace vault {

namespace detail {

// PmidName GetPmidAccountName(const nfs::Message& message) {
//  return PmidName(Identity(message.data().name));
//}

template <typename Message>
inline bool ForThisPersona(const Message& message) {
  return message.destination_persona() != nfs::Persona::kPmidManager;
}

}  // namespace detail

PmidManagerService::PmidManagerService(const passport::Pmid& /*pmid*/, routing::Routing& routing)
    : routing_(routing), group_db_(), accumulator_mutex_(), accumulator_(), dispatcher_(routing_),
      sync_puts_(), sync_deletes_(), sync_set_available_sizes_() {}


void PmidManagerService::HandleSyncedPut(
    std::unique_ptr<PmidManager::UnresolvedPut>&& synced_action) {
  LOG(kVerbose) << "PmidManagerService::HandleSyncedPut commit put for chunk "
                << HexSubstr(synced_action->key.name.string())
                << " to group_db_ and send_put_response";
  group_db_.Commit(synced_action->key, synced_action->action);
  auto data_name(GetDataNameVariant(synced_action->key.type, synced_action->key.name));
  SendPutResponse(data_name, synced_action->key.group_name(),
                  synced_action->action.kSize,
                  synced_action->action.kMessageId);
}


void PmidManagerService::HandleSyncedDelete(
    std::unique_ptr<PmidManager::UnresolvedDelete>&& synced_action) {
  LOG(kVerbose) << "PmidManagerService::HandleSyncedDelete commit delete for chunk "
                << HexSubstr(synced_action->key.name.string()) << " to group_db_ ";
  group_db_.Commit(synced_action->key, synced_action->action);
}

// =============== Sync ============================================================================

void PmidManagerService::DoSync() {
  detail::IncrementAttemptsAndSendSync(dispatcher_, sync_puts_);
  detail::IncrementAttemptsAndSendSync(dispatcher_, sync_deletes_);
  detail::IncrementAttemptsAndSendSync(dispatcher_, sync_set_available_sizes_);
}

// =============== HandleMessage ===================================================================

template <>
void PmidManagerService::HandleMessage(
    const PutRequestFromDataManagerToPmidManager& message,
    const typename PutRequestFromDataManagerToPmidManager::Sender& sender,
    const typename PutRequestFromDataManagerToPmidManager::Receiver& receiver) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage PutRequestFromDataManagerToPmidManager"
                << " from " << HexSubstr(sender.sender_id->string())
                << " being asked send to " << HexSubstr(receiver->string());
  typedef PutRequestFromDataManagerToPmidManager MessageType;
  OperationHandlerWrapper<PmidManagerService, MessageType>(
      accumulator_, [this](const MessageType & message, const MessageType::Sender & sender) {
                      return this->ValidateSender(message, sender);
                    },
      Accumulator<Messages>::AddRequestChecker(RequiredRequests(message)),
      this, accumulator_mutex_)(message, sender, receiver);
}

template <>
void PmidManagerService::HandleMessage(
    const PutFailureFromPmidNodeToPmidManager& message,
    const typename PutFailureFromPmidNodeToPmidManager::Sender& sender,
    const typename PutFailureFromPmidNodeToPmidManager::Receiver& receiver) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage PutFailureFromPmidNodeToPmidManager";
  typedef PutFailureFromPmidNodeToPmidManager MessageType;
  OperationHandlerWrapper<PmidManagerService, MessageType>(
      accumulator_, [this](const MessageType& message, const MessageType::Sender & sender) {
                      return this->ValidateSender(message, sender);
                    },
      Accumulator<Messages>::AddRequestChecker(RequiredRequests(message)),
      this, accumulator_mutex_)(message, sender, receiver);
}

template <>
void PmidManagerService::HandleMessage(
    const nfs::PmidHealthRequestFromMaidNodeToPmidManager& message,
    const typename nfs::PmidHealthRequestFromMaidNodeToPmidManager::Sender& sender,
    const typename nfs::PmidHealthRequestFromMaidNodeToPmidManager::Receiver& receiver) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage PmidHealthRequestFromMaidNodeToPmidManager";
  typedef nfs::PmidHealthRequestFromMaidNodeToPmidManager MessageType;
  OperationHandlerWrapper<PmidManagerService, MessageType>(
      accumulator_, [this](const MessageType& message, const MessageType::Sender & sender) {
                      return this->ValidateSender(message, sender);
                    },
      Accumulator<Messages>::AddRequestChecker(RequiredRequests(message)),
      this, accumulator_mutex_)(message, sender, receiver);
}

template <>
void PmidManagerService::HandleMessage(
    const DeleteRequestFromDataManagerToPmidManager& message,
    const typename DeleteRequestFromDataManagerToPmidManager::Sender& sender,
    const typename DeleteRequestFromDataManagerToPmidManager::Receiver& receiver) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage DeleteRequestFromDataManagerToPmidManager";
  typedef DeleteRequestFromDataManagerToPmidManager MessageType;
  OperationHandlerWrapper<PmidManagerService, MessageType>(
      accumulator_, [this](const MessageType& message, const MessageType::Sender & sender) {
                      return this->ValidateSender(message, sender);
                    },
      Accumulator<Messages>::AddRequestChecker(RequiredRequests(message)),
      this, accumulator_mutex_)(message, sender, receiver);
}

template <>
void PmidManagerService::HandleMessage(
    const GetPmidAccountRequestFromPmidNodeToPmidManager& message,
    const typename GetPmidAccountRequestFromPmidNodeToPmidManager::Sender& sender,
    const typename GetPmidAccountRequestFromPmidNodeToPmidManager::Receiver& receiver) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage GetPmidAccountRequestFromPmidNodeToPmidManager";
  typedef GetPmidAccountRequestFromPmidNodeToPmidManager MessageType;
  OperationHandlerWrapper<PmidManagerService, MessageType>(
      accumulator_, [this](const MessageType& message, const MessageType::Sender & sender) {
                      return this->ValidateSender(message, sender);
                    },
      Accumulator<Messages>::AddRequestChecker(RequiredRequests(message)),
      this, accumulator_mutex_)(message, sender, receiver);
}

// =============== Handle Sync Messages ============================================================

template<>
void PmidManagerService::HandleMessage(
    const SynchroniseFromPmidManagerToPmidManager& message,
    const typename SynchroniseFromPmidManagerToPmidManager::Sender& sender,
    const typename SynchroniseFromPmidManagerToPmidManager::Receiver& /*receiver*/) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage SynchroniseFromPmidManagerToPmidManager";
  protobuf::Sync proto_sync;
  if (!proto_sync.ParseFromString(message.contents->data)) {
    LOG(kError) << "SynchroniseFromPmidManagerToPmidManager can't parse content";
    ThrowError(CommonErrors::parsing_error);
  }
  switch (static_cast<nfs::MessageAction>(proto_sync.action_type())) {
    case ActionPmidManagerPut::kActionId: {
      LOG(kVerbose) << "SynchroniseFromPmidManagerToPmidManager ActionPmidManagerPut";
      PmidManager::UnresolvedPut unresolved_action(
          proto_sync.serialised_unresolved_action(), sender.sender_id, routing_.kNodeId());
      auto resolved_action(sync_puts_.AddUnresolvedAction(unresolved_action));
      if (resolved_action) {
        HandleSyncedPut(std::move(resolved_action));
      }
      break;
    }
    case ActionPmidManagerDelete::kActionId: {
      LOG(kVerbose) << "SynchroniseFromPmidManagerToPmidManager ActionPmidManagerDelete";
      PmidManager::UnresolvedDelete unresolved_action(
          proto_sync.serialised_unresolved_action(), sender.sender_id, routing_.kNodeId());
      auto resolved_action(sync_deletes_.AddUnresolvedAction(unresolved_action));
      if (resolved_action) {
        LOG(kInfo) << "SynchroniseFromPmidManagerToPmidManager SendDeleteRequest";
        HandleSyncedDelete(std::move(resolved_action));
      }
      break;
    }
    default: {
      LOG(kError) << "Unhandled action type";
      assert(false);
    }
  }
}

template<>
void PmidManagerService::HandleMessage(
    const AccountTransferFromPmidManagerToPmidManager& /*message*/,
    const typename AccountTransferFromPmidManagerToPmidManager::Sender& /*sender*/,
    const typename AccountTransferFromPmidManagerToPmidManager::Receiver& /*receiver*/) {
  LOG(kVerbose) << "PmidManagerService::HandleMessage AccountTransferFromPmidManagerToPmidManager";
  assert(0);
}

//=================================================================================================

void PmidManagerService::SendPutResponse(const DataNameVariant& data_name,
                                         const PmidName& pmid_node, int32_t size,
                                         nfs::MessageId message_id) {
  LOG(kInfo) << "PmidManagerService::SendPutResponse";
  detail::PmidManagerPutResponseVisitor<PmidManagerService> put_response(this, size, pmid_node,
                                                                         message_id);
  boost::apply_visitor(put_response, data_name);
}

//=================================================================================================

void PmidManagerService::HandleSendPmidAccount(const PmidName& pmid_node, int64_t available_size) {
  std::vector<nfs_vault::DataName> data_names;
  try {
    auto contents(group_db_.GetContents(pmid_node));
    for (const auto& kv_pair : contents.kv_pair)
      data_names.push_back(nfs_vault::DataName(kv_pair.first.type, kv_pair.first.name));
    dispatcher_.SendPmidAccount(pmid_node, data_names,
                                nfs_client::ReturnCode(CommonErrors::success));
    sync_set_available_sizes_.AddLocalAction(PmidManager::UnresolvedSetAvailableSize(
        PmidManager::MetadataKey(pmid_node), ActionPmidManagerSetAvailableSize(available_size),
        routing_.kNodeId()));
    DoSync();
  } catch (const vault_error& error) {
    if (error.code().value() != static_cast<int>(VaultErrors::no_such_account))
      throw error;
    dispatcher_.SendPmidAccount(pmid_node, data_names,
                                nfs_client::ReturnCode(VaultErrors::no_such_account));
  }
}

void PmidManagerService::HandleHealthRequest(const PmidName& pmid_node,
                                             const MaidName& maid_node,
                                             nfs::MessageId message_id) {
  LOG(kVerbose) << "PmidManagerService::HandleHealthRequest from maid_node "
                << HexSubstr(maid_node.value.string()) << " for pmid_node "
                << HexSubstr(pmid_node.value.string()) << " with message_id " << message_id.data;
  try {
    // BEFORE_RELEASE shall replace dummy with pmid_metadata_.at(pmid_node)
    PmidManagerMetadata dummy(pmid_node);
    dummy.SetAvailableSize(100000000);
    dispatcher_.SendHealthResponse(maid_node, pmid_node, dummy,
                                   message_id, maidsafe_error(CommonErrors::success));
  }
  catch(...) {
    LOG(kInfo) << "PmidManagerService::HandleHealthRequest no_such_element";
    dispatcher_.SendHealthResponse(maid_node, pmid_node, PmidManagerMetadata(), message_id,
                                   maidsafe_error(CommonErrors::no_such_element));
  }
}

// =================================================================================================

// void PmidManagerService::GetPmidTotals(const nfs::Message& message) {
//  try {
//    PmidManagerMetadata
// metadata(pmid_account_handler_.GetMetadata(PmidName(message.data().name)));
//    if (!metadata.pmid_name->string().empty()) {
//      nfs::Reply reply(CommonErrors::success, metadata.Serialise());
//      nfs_.ReturnPmidTotals(message.source().node_id, reply.Serialise());
//    } else {
//      nfs_.ReturnFailure(message);
//    }
//  }
//  catch(const maidsafe_error& error) {
//    LOG(kWarning) << error.what();
//  }
//  catch(...) {
//    LOG(kWarning) << "Unknown error.";
//  }
//}

// void PmidManagerService::GetPmidAccount(const nfs::Message& message) {
//  try {
//    PmidName pmid_name(detail::GetPmidAccountName(message));
//    protobuf::PmidAccountResponse pmid_account_response;
//    protobuf::PmidAccount pmid_account;
//    PmidAccount::serialised_type serialised_account_details;
//    pmid_account.set_pmid_name(pmid_name.data.string());
//    try {
//      serialised_account_details = pmid_account_handler_.GetSerialisedAccount(pmid_name, false);
//      pmid_account.set_serialised_account_details(serialised_account_details.data.string());
//      pmid_account_response.set_status(true);
//    }
//    catch(const maidsafe_error&) {
//      pmid_account_response.set_status(false);
//      pmid_account_handler_.CreateAccount(PmidName(detail::GetPmidAccountName(message)));
//    }
//    pmid_account_response.mutable_pmid_account()->CopyFrom(pmid_account);
//    nfs_.AccountTransfer<passport::Pmid>(
//          pmid_name, NonEmptyString(pmid_account_response.SerializeAsString()));
//  }
//  catch(const maidsafe_error& error) {
//    LOG(kWarning) << error.what();
//  }
//  catch(...) {
//    LOG(kWarning) << "Unknown error.";
//  }
//}

void PmidManagerService::HandleChurnEvent(std::shared_ptr<routing::MatrixChange> /*matrix_change*/) {
//  auto account_names(pmid_account_handler_.GetAccountNames());
//  auto itr(std::begin(account_names));
//  while (itr != std::end(account_names)) {
//    auto check_holders_result(matrix_change->CheckHolders(NodeId((*itr)->string())));
//    // Delete accounts for which this node is no longer responsible.
//    if (check_holders_result.proximity_status != routing::GroupRangeStatus::kInRange) {
//      pmid_account_handler_.DeleteAccount(*itr);
//      itr = account_names.erase(itr);
//      continue;
//    }
//    // Replace old_node(s) in sync object and send AccountTransfer to new node(s).
//    assert(check_holders_result.old_holders.size() == check_holders_result.new_holders.size());
//    for (auto i(0U); i != check_holders_result.old_holders.size(); ++i) {
//      pmid_account_handler_.ReplaceNodeInSyncList(*itr, check_holders_result.old_holders[i],
//                                                  check_holders_result.new_holders[i]);
//      TransferAccount(*itr, check_holders_result.new_holders[i]);
//    }

//    ++itr;
//  }
  assert(0);
}

// void PmidManagerService::ValidateDataSender(const nfs::Message& message) const {
//  if (!message.HasDataHolder()
//      || !routing_.IsConnectedVault(NodeId(message.pmid_node()->string()))
//      || routing_.EstimateInGroup(message.source().node_id, NodeId(message.data().name)))
//    ThrowError(VaultErrors::permission_denied);

//  if (!FromDataManager(message) || !detail::ForThisPersona(message))
//    ThrowError(CommonErrors::invalid_parameter);
//}

// void PmidManagerService::ValidateGenericSender(const nfs::Message& message) const {
//  if (!routing_.IsConnectedVault(message.source().node_id)
//      || routing_.EstimateInGroup(message.source().node_id, NodeId(message.data().name)))
//    ThrowError(VaultErrors::permission_denied);

//  if (!FromDataManager(message) || !FromPmidNode(message) || !detail::ForThisPersona(message))
//    ThrowError(CommonErrors::invalid_parameter);
//}

// =============== Account transfer ===============================================================

// void PmidManagerService::TransferAccount(const PmidName& account_name, const NodeId& new_node) {
//  protobuf::PmidAccount pmid_account;
//  pmid_account.set_pmid_name(account_name.data.string());
//  PmidAccount::serialised_type
//    serialised_account_details(pmid_account_handler_.GetSerialisedAccount(account_name, true));
//  pmid_account.set_serialised_account_details(serialised_account_details.data.string());
//  nfs_.TransferAccount(new_node, NonEmptyString(pmid_account.SerializeAsString()));
//}

// void PmidManagerService::HandleAccountTransfer(const nfs::Message& message) {
//  protobuf::PmidAccount pmid_account;
//  NodeId source_id(message.source().node_id);
//  if (!pmid_account.ParseFromString(message.data().content.string()))
//    return;

//  PmidName account_name(Identity(pmid_account.pmid_name()));
//  bool finished_all_transfers(
//      pmid_account_handler_.ApplyAccountTransfer(account_name, source_id,
//         PmidAccount::serialised_type(NonEmptyString(pmid_account.serialised_account_details()))));
//  if (finished_all_transfers)
//    return;    // TODO(Team) Implement whatever else is required here?
//}

}  // namespace vault
}  // namespace maidsafe
