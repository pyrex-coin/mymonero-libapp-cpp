//
//  WalletsListController_Base.cpp
//  MyMonero
//
//  Copyright (c) 2014-2019, MyMonero.com
//
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are
//  permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice, this list of
//	conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice, this list
//	of conditions and the following disclaimer in the documentation and/or other
//	materials provided with the distribution.
//
//  3. Neither the name of the copyright holder nor the names of its contributors may be
//	used to endorse or promote products derived from this software without specific
//	prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
//  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
//  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
//  THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
//  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
//  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//
#include "WalletsListController_Base.hpp"
#include <boost/foreach.hpp>
#include "misc_log_ex.h"
#include "../Pyrex-core-cpp/src/monero_wallet_utils.hpp"
using namespace Wallets;
using namespace monero_wallet_utils;
//
// Lifecycle - Init
void ListController_Base::setup()
{
	if (documentsPath == nullptr) {
		BOOST_THROW_EXCEPTION(logic_error("ListController: expected documentsPath != nullptr"));
	}
	if (userIdleController == nullptr) {
		BOOST_THROW_EXCEPTION(logic_error("ListController: expected userIdleController != nullptr"));
	}
	if (ccyConversionRatesController == nullptr) {
		BOOST_THROW_EXCEPTION(logic_error("ListController: expected ccyConversionRatesController != nullptr"));
	}
	// check deps *first* before calling on super
	Lists::Controller::setup();
}
void ListController_Base::setup_startObserving()
{
	Lists::Controller::setup_startObserving();
	//
	connection__HostedMonero_initializedWithNewServerURL = apiClient->initializedWithNewServerURL_signal.connect(
		std::bind(&ListController_Base::HostedMonero_initializedWithNewServerURL, this)
	);
}
void ListController_Base::stopObserving()
{
	Lists::Controller::stopObserving();
	//
	connection__HostedMonero_initializedWithNewServerURL.disconnect();
}
//
// Accessors - Derived properties
std::vector<Wallets::SwatchColor> ListController_Base::givenBooted_swatchesInUse()
{
	if (_hasBooted != true) {
		BOOST_THROW_EXCEPTION(logic_error("givenBooted_swatchesInUse called when \(self) not yet booted."));
		return std::vector<Wallets::SwatchColor>(); // this may be for the first wallet creation - let's say nothing in use yet
	}
	std::vector<Wallets::SwatchColor> inUseSwatches;
	for (std::vector<std::shared_ptr<Persistable::Object>>::iterator it = _records.begin(); it != _records.end(); ++it) {
		inUseSwatches.push_back(std::dynamic_pointer_cast<Wallets::Object>(*it)->swatchColor());
	}
	return inUseSwatches;
}
//
// Booted - Imperatives - Public - Wallets list
void ListController_Base::CreateNewWallet_NoBootNoListAdd(
	string localeCode,
	std::function<void(optional<string> err, std::shared_ptr<Wallets::Object> walletInstance)> fn
) {
	monero_wallet_utils::WalletDescriptionRetVals retVals;
	bool r = monero_wallet_utils::convenience__new_wallet_with_language_code(
		localeCode,
		retVals,
		_nettype
	);
	bool did_error = retVals.did_error;
	if (!r) {
		fn(std::move(*(retVals.err_string)), nullptr);
		return;
	}
	if (did_error) {
		string err_str = "Illegal success flag but did_error";
		BOOST_THROW_EXCEPTION(logic_error(err_str));
		return;
	}
	fn(none, std::make_shared<Wallets::Object>(
		documentsPath,
		passwordController,
		std::move(*(retVals.optl__desc)),
		_nettype,
		apiClient,
		dispatch_ptr,
		userIdleController,
		ccyConversionRatesController
	));
}
void ListController_Base::OnceBooted_ObtainPW_AddNewlyGeneratedWallet(
	std::shared_ptr<Wallets::Object> walletInstance,
	string walletLabel,
	Wallets::SwatchColor swatchColor,
	std::function<void(optional<string> err_str, std::shared_ptr<Wallets::Object> walletInstance)>&& fn,
	std::function<void()>&& userCanceledPasswordEntry_fn
) {
	std::weak_ptr<Wallets::Object> weak_wallet(walletInstance);
	std::shared_ptr<ListController_Base> shared_this = shared_from_this();
	std::weak_ptr<ListController_Base> weak_this = shared_this;
	onceBooted([
		weak_this, weak_wallet, walletLabel, swatchColor,
		fn = std::move(fn), userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
	] () {
		if (auto inner_spt = weak_this.lock()) {
			inner_spt->passwordController->onceBootedAndPasswordObtained([
				weak_this, weak_wallet, walletLabel, swatchColor,
				 fn = std::move(fn), userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
				] (
					Passwords::Password password,
					Passwords::Type type
				) {
					if (auto inner_inner_spt = weak_this.lock()) {
						auto walletInstance = weak_wallet.lock();
						if (walletInstance) {
							walletInstance->Boot_byLoggingIn_givenNewlyCreatedWallet(
								walletLabel,
								swatchColor,
								[weak_this, fn = std::move(fn), weak_wallet](optional<string> err_str)
								{
									if (err_str != none) {
										fn(*err_str, nullptr);
										return;
									}
									if (auto inner_inner_inner_spt = weak_this.lock()) {
										auto walletInstance = weak_wallet.lock();
										if (walletInstance) {
											inner_inner_inner_spt->_atRuntime__record_wasSuccessfullySetUp(walletInstance);
											//
											fn(none, walletInstance);
										}
										MWARNING("Wallet instance freed during Boot_byLoggingIn_givenNewlyCreatedWallet");
									}
								}
							);
						} else {
							MWARNING("Wallet instance freed during onceBootedAndPasswordObtained");
						}
					}
				},
				[
					userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
				] (void) { // user canceled
					userCanceledPasswordEntry_fn();
				}
			);
		}
	});
}
void ListController_Base::OnceBooted_ObtainPW_AddExtantWalletWith_MnemonicString(
	string walletLabel,
	Wallets::SwatchColor swatchColor,
	string mnemonicString,
	std::function<void(
		optional<string> err_str,
		optional<std::shared_ptr<Wallets::Object>> walletInstance,
		optional<bool> wasWalletAlreadyInserted
	)> fn,
	std::function<void()> userCanceledPasswordEntry_fn
) {
	std::shared_ptr<ListController_Base> shared_this = shared_from_this();
	std::weak_ptr<ListController_Base> weak_this = shared_this;
	onceBooted([
		weak_this, walletLabel, mnemonicString, swatchColor,
		fn = std::move(fn), userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
	] () {
		if (auto inner_spt = weak_this.lock()) {
			inner_spt->passwordController->onceBootedAndPasswordObtained([
				weak_this, walletLabel, mnemonicString, swatchColor,
				fn = std::move(fn), userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
			] (Passwords::Password password, Passwords::Type type) {
				if (auto inner_inner_spt = weak_this.lock()) {
					{ // check if wallet already entered
						for (std::vector<std::shared_ptr<Persistable::Object>>::iterator it = inner_inner_spt->_records.begin(); it != inner_inner_spt->_records.end(); ++it) {
							auto wallet = std::dynamic_pointer_cast<Wallets::Object>(*it);
							if (wallet->mnemonicString() != none && wallet->mnemonicString()->size()) {
								// TODO: solve limitation of this code - check if wallet with same address (but no mnemonic) was already added
								continue;
							}
							bool equal;
							try {
								equal = are_equal_mnemonics(*(wallet->mnemonicString()), mnemonicString);
							} catch (std::exception const& e) {
								fn(string(e.what()), none, none);
								return;
							}
							if (equal) { // would be rather odd; NOTE: must use this comparator instead of string comparison to support partial-word mnemonic strings
								fn(none, wallet, true); // wasWalletAlreadyInserted: true
								return;
							}
						}
					}
					auto wallet = std::make_shared<Wallets::Object>(
						inner_inner_spt->documentsPath,
						inner_inner_spt->passwordController,
						none,
						inner_inner_spt->_nettype,
						inner_inner_spt->apiClient,
						inner_inner_spt->dispatch_ptr,
						inner_inner_spt->userIdleController,
						inner_inner_spt->ccyConversionRatesController
					);
					wallet->Boot_byLoggingIn_existingWallet_withMnemonic(
						walletLabel,
						swatchColor,
						mnemonicString,
						false, // persistEvenIfLoginFailed_forServerChange
						[wallet, weak_this, fn = std::move(fn)] (optional<string> err_str) {
							if (auto inner_inner_inner_spt = weak_this.lock()) {
								if (err_str != none) {
									fn(err_str, none, none);
									return;
								}
								inner_inner_inner_spt->_atRuntime__record_wasSuccessfullySetUp(wallet);
								fn(none, wallet, false); // wasWalletAlreadyInserted: false
							}
						}
					);
				} else {
					return; // for debugger
				}
			},
			[userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)] (void)
			{ // user canceled
				userCanceledPasswordEntry_fn();
			});
		} else {
			return; // for debugger
		}
	});
}
void ListController_Base::OnceBooted_ObtainPW_AddExtantWalletWith_AddressAndKeys(
	string walletLabel,
	Wallets::SwatchColor swatchColor,
	string address,
	string sec_view_key,
	string sec_spend_key,
	std::function<void(
		optional<string> err_str,
		optional<std::shared_ptr<Wallets::Object>> walletInstance,
		optional<bool> wasWalletAlreadyInserted
	)> fn,
	std::function<void()> userCanceledPasswordEntry_fn
) {
	std::shared_ptr<ListController_Base> shared_this = shared_from_this();
	std::weak_ptr<ListController_Base> weak_this = shared_this;
	onceBooted([
		weak_this, walletLabel, address, sec_view_key, sec_spend_key, swatchColor,
		fn = std::move(fn), userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
	] () {
		if (auto inner_spt = weak_this.lock()) {
			inner_spt->passwordController->onceBootedAndPasswordObtained([
				weak_this, walletLabel, address, sec_view_key, sec_spend_key, swatchColor,
				fn = std::move(fn), userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)
			] (Passwords::Password password, Passwords::Type type) {
				if (auto inner_inner_spt = weak_this.lock()) {
					{ // check if wallet already entered
						for (std::vector<std::shared_ptr<Persistable::Object>>::iterator it = inner_inner_spt->_records.begin(); it != inner_inner_spt->_records.end(); ++it) {
							auto wallet = std::dynamic_pointer_cast<Wallets::Object>(*it);
							if (wallet->public_address() == address) {
								// simply return existing wallet; note: this wallet might have mnemonic and thus seed
								// so might not be exactly what consumer of GivenBooted_ObtainPW_AddExtantWalletWith_AddressAndKeys is expecting
								fn(none, wallet, true); // wasWalletAlreadyInserted: true
								return;
							}
						}
					}
					auto wallet = std::make_shared<Wallets::Object>(
						inner_inner_spt->documentsPath,
						inner_inner_spt->passwordController,
						none,
						inner_inner_spt->_nettype,
						inner_inner_spt->apiClient,
						inner_inner_spt->dispatch_ptr,
						inner_inner_spt->userIdleController,
						inner_inner_spt->ccyConversionRatesController
					);
					wallet->Boot_byLoggingIn_existingWallet_withAddressAndKeys(
						walletLabel,
						swatchColor,
						address,
						sec_view_key,
						sec_spend_key,
						false, // persistEvenIfLoginFailed_forServerChange
						[wallet, weak_this, fn = std::move(fn)] (optional<string> err_str)
						{
							if (auto inner_inner_inner_spt = weak_this.lock()) {
								if (err_str != none) {
									fn(err_str, none, none);
									return;
								}
								inner_inner_inner_spt->_atRuntime__record_wasSuccessfullySetUp(wallet);
								fn(none, wallet, false); // wasWalletAlreadyInserted: false
							}
						}
					);
				}
			},
			[userCanceledPasswordEntry_fn = std::move(userCanceledPasswordEntry_fn)] (void)
			{ // user canceled
				userCanceledPasswordEntry_fn();
			});
		}
	});
}
//
// Delegation - Signals
void ListController_Base::HostedMonero_initializedWithNewServerURL()
{
	// 'log out' all wallets by deleting their runtime state, then reboot them
	if (_hasBooted == false) {
		if (_records.size() != 0) {
			BOOST_THROW_EXCEPTION(logic_error("Expected _records.size == 0"));
		}
		return; // nothing to do
	}
	if (_records.size() == 0) {
		return; // nothing to do
	}
	if (passwordController->hasUserEnteredValidPasswordYet() == false) {
		BOOST_THROW_EXCEPTION(logic_error("App expected password to exist as wallets exist"));
		return;
	}
	for (std::vector<std::shared_ptr<Persistable::Object>>::iterator it = _records.begin(); it != _records.end(); ++it) {
		std::dynamic_pointer_cast<Wallets::Object>(*it)->logOutThenSaveAndLogIn();
	}
	__dispatchAsync_listUpdated_records(); // probably not necessary
}
