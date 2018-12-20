#include <filezilla.h>
#include "loginmanager.h"

#include "dialogex.h"
#include "filezillaapp.h"
#include "Options.h"
#include "xrc_helper.h"

#include <algorithm>

CLoginManager CLoginManager::m_theLoginManager;

std::list<CLoginManager::t_passwordcache>::iterator CLoginManager::FindItem(CServer const& server, std::wstring const& challenge)
{
	return std::find_if(m_passwordCache.begin(), m_passwordCache.end(), [&](t_passwordcache const& item)
		{
			return item.host == server.GetHost() && item.port == server.GetPort() && item.user == server.GetUser() && item.challenge == challenge;
		}
	);
}

bool CLoginManager::GetPassword(Site & site, bool silent, std::wstring const& name)
{
	bool const needsUser = ProtocolHasUser(site.server_.server.GetProtocol()) && site.server_.server.GetUser().empty() && (site.server_.credentials.logonType_ == LogonType::ask || site.server_.credentials.logonType_ == LogonType::interactive);

	if (site.server_.credentials.logonType_ != LogonType::ask && !site.server_.credentials.encrypted_ && !needsUser) {
		return true;
	}

	if (site.server_.credentials.encrypted_) {
		auto priv = decryptors_.find(site.server_.credentials.encrypted_);
		if (priv != decryptors_.end() && priv->second) {
			return site.server_.credentials.Unprotect(priv->second);
		}

		if (!silent) {
			return DisplayDialogForEncrypted(site, name);
		}
	}
	else {
		auto it = FindItem(site.server_.server, std::wstring());
		if (it != m_passwordCache.end()) {
			site.server_.credentials.SetPass(it->password);
			return true;
		}

		if (!silent) {
			return DisplayDialog(site, name, std::wstring(), true);
		}
	}

	return false;
}


bool CLoginManager::GetPassword(Site & site, bool silent, std::wstring const& name, std::wstring const& challenge, bool canRemember)
{
	if (canRemember) {
		auto it = FindItem(site.server_.server, challenge);
		if (it != m_passwordCache.end()) {
			site.server_.credentials.SetPass(it->password);
			return true;
		}
	}
	if (silent) {
		return false;
	}

	return DisplayDialog(site, name, challenge, canRemember);
}

bool CLoginManager::DisplayDialogForEncrypted(Site & site, std::wstring const& name)
{
	assert(site.server_.credentials.encrypted_);

	wxDialogEx pwdDlg;
	if (!pwdDlg.Load(wxGetApp().GetTopWindow(), _T("ID_ENTERMASTERPASSWORD"))) {
		return false;
	}

	if (name.empty()) {
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_NAMELABEL", wxStaticText), false, true);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_NAME", wxStaticText), false, true);
	}
	else {
		xrc_call(pwdDlg, "ID_NAME", &wxStaticText::SetLabel, name);
	}

	XRCCTRL(pwdDlg, "ID_HOST", wxStaticText)->SetLabel(site.server_.Format(ServerFormat::with_optional_port));

	XRCCTRL(pwdDlg, "ID_OLD_USER", wxStaticText)->SetLabel(site.server_.server.GetUser());

	XRCCTRL(pwdDlg, "wxID_OK", wxButton)->SetId(wxID_OK);
	XRCCTRL(pwdDlg, "wxID_CANCEL", wxButton)->SetId(wxID_CANCEL);

	xrc_call(pwdDlg, "ID_KEY_IDENTIFIER", &wxStaticText::SetLabel, site.server_.credentials.encrypted_.to_base64().substr(0, 8));

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		auto pass = fz::to_utf8(xrc_call(pwdDlg, "ID_PASSWORD", &wxTextCtrl::GetValue));
		auto key = fz::private_key::from_password(pass, site.server_.credentials.encrypted_.salt_);

		if (key.pubkey() != site.server_.credentials.encrypted_) {
			wxMessageBoxEx(_("Wrong master password entered, it cannot be used to decrypt this item."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		if (!site.server_.credentials.Unprotect(key)) {
			wxMessageBoxEx(_("Failed to decrypt server password."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		if (xrc_call(pwdDlg, "ID_REMEMBER", &wxCheckBox::IsChecked)) {
			Remember(key);
		}
		break;
	}

	return true;
}

bool CLoginManager::DisplayDialog(Site & site, std::wstring const& name, std::wstring const& challenge, bool canRemember)
{
	assert(!site.server_.credentials.encrypted_);

	wxDialogEx pwdDlg;
	if (!pwdDlg.Load(wxGetApp().GetTopWindow(), _T("ID_ENTERPASSWORD"))) {
		return false;
	}

	if (name.empty()) {
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_NAMELABEL", wxStaticText), false, true);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_NAME", wxStaticText), false, true);
	}
	else {
		xrc_call(pwdDlg, "ID_NAME", &wxStaticText::SetLabel, name);
	}

	if (challenge.empty()) {
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_CHALLENGELABEL", wxStaticText), false, true);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_CHALLENGE", wxTextCtrl), false, true);

	}
	else {
		wxString displayChallenge = challenge;
		displayChallenge.Trim(true);
		displayChallenge.Trim(false);
#ifdef FZ_WINDOWS
		displayChallenge.Replace(_T("\n"), _T("\r\n"));
#endif
		XRCCTRL(pwdDlg, "ID_CHALLENGE", wxTextCtrl)->ChangeValue(displayChallenge);
		pwdDlg.GetSizer()->Show(XRCCTRL(pwdDlg, "ID_REMEMBER", wxCheckBox), canRemember, true);
		XRCCTRL(pwdDlg, "ID_CHALLENGE", wxTextCtrl)->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
	}
	XRCCTRL(pwdDlg, "ID_HOST", wxStaticText)->SetLabel(site.server_.Format(ServerFormat::with_optional_port));

	if (site.server_.server.GetUser().empty()) {
		XRCCTRL(pwdDlg, "ID_OLD_USER_LABEL", wxStaticText)->Hide();
		XRCCTRL(pwdDlg, "ID_OLD_USER", wxStaticText)->Hide();

		XRCCTRL(pwdDlg, "ID_HEADER_PASS", wxStaticText)->Hide();
		if (site.server_.credentials.logonType_ == LogonType::interactive) {
			pwdDlg.SetTitle(_("Enter username"));
			XRCCTRL(pwdDlg, "ID_PASSWORD_LABEL", wxStaticText)->Hide();
			XRCCTRL(pwdDlg, "ID_PASSWORD", wxTextCtrl)->Hide();
			XRCCTRL(pwdDlg, "ID_HEADER_BOTH", wxStaticText)->Hide();

			canRemember = false;
			XRCCTRL(pwdDlg, "ID_REMEMBER", wxCheckBox)->Hide();
		}
		else {
			pwdDlg.SetTitle(_("Enter username and password"));
			XRCCTRL(pwdDlg, "ID_HEADER_USER", wxStaticText)->Hide();
		}
		XRCCTRL(pwdDlg, "ID_NEW_USER", wxTextCtrl)->SetFocus();
	}
	else {
		XRCCTRL(pwdDlg, "ID_OLD_USER", wxStaticText)->SetLabel(site.server_.server.GetUser());
		XRCCTRL(pwdDlg, "ID_NEW_USER_LABEL", wxStaticText)->Hide();
		XRCCTRL(pwdDlg, "ID_NEW_USER", wxTextCtrl)->Hide();
		XRCCTRL(pwdDlg, "ID_HEADER_USER", wxStaticText)->Hide();
		xrc_call(pwdDlg, "ID_HEADER_BOTH", &wxStaticText::Hide);
	}

	if (site.server_.server.GetProtocol() == STORJ) {
		XRCCTRL(pwdDlg, "ID_ENCRYPTIONKEY_LABEL", wxStaticText)->Show();
		XRCCTRL(pwdDlg, "ID_ENCRYPTIONKEY", wxTextCtrl)->Show();
	}

	XRCCTRL(pwdDlg, "wxID_OK", wxButton)->SetId(wxID_OK);
	XRCCTRL(pwdDlg, "wxID_CANCEL", wxButton)->SetId(wxID_CANCEL);
	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		if (site.server_.server.GetUser().empty()) {
			auto user = xrc_call(pwdDlg, "ID_NEW_USER", &wxTextCtrl::GetValue).ToStdWstring();
			if (user.empty()) {
				wxMessageBoxEx(_("No username given."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
			site.server_.server.SetUser(user);
		}

/* FIXME?
	if (site.server_.server.GetProtocol() == STORJ) {
		std::wstring encryptionKey = XRCCTRL(pwdDlg, "ID_ENCRYPTIONKEY", wxTextCtrl)->GetValue().ToStdWstring();
		if (encryptionKey.empty()) {
			wxMessageBoxEx(_("No encryption key given."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}
	}
*/
		std::wstring pass = xrc_call(pwdDlg, "ID_PASSWORD", &wxTextCtrl::GetValue).ToStdWstring();
		if (site.server_.server.GetProtocol() == STORJ) {
			std::wstring encryptionKey = xrc_call(pwdDlg, "ID_ENCRYPTIONKEY", &wxTextCtrl::GetValue).ToStdWstring();
			pass += L"|" + encryptionKey;
		}
		site.server_.credentials.SetPass(pass);

		if (canRemember && xrc_call(pwdDlg, "ID_REMEMBER", &wxCheckBox::IsChecked)) {
			RememberPassword(site, challenge);
		}
		break;
	}

	return true;
}

void CLoginManager::CachedPasswordFailed(CServer const& server, std::wstring const& challenge)
{
	auto it = FindItem(server, challenge);
	if (it != m_passwordCache.end()) {
		m_passwordCache.erase(it);
	}
}

void CLoginManager::RememberPassword(Site & site, std::wstring const& challenge)
{
	if (site.server_.credentials.logonType_ == LogonType::anonymous) {
		return;
	}

	auto it = FindItem(site.server_.server, challenge);
	if (it != m_passwordCache.end()) {
		it->password = site.server_.credentials.GetPass();
	}
	else {
		t_passwordcache entry;
		entry.host = site.server_.server.GetHost();
		entry.port = site.server_.server.GetPort();
		entry.user = site.server_.server.GetUser();
		entry.password = site.server_.credentials.GetPass();
		entry.challenge = challenge;
		m_passwordCache.push_back(entry);
	}
}

bool CLoginManager::AskDecryptor(fz::public_key const& pub, bool allowForgotten, bool allowCancel)
{
	if (this == &CLoginManager::Get()) {
		return false;
	}

	if (!pub) {
		return false;
	}

	if (decryptors_.find(pub) != decryptors_.cend()) {
		return true;
	}

	wxDialogEx pwdDlg;
	if (!pwdDlg.Load(wxGetApp().GetTopWindow(), _T("ID_ENTEROLDMASTERPASSWORD"))) {
		return false;
	}
	xrc_call(pwdDlg, "ID_KEY_IDENTIFIER", &wxStaticText::SetLabel, pub.to_base64().substr(0, 8));

	if (!allowForgotten) {
		xrc_call(pwdDlg, "ID_FORGOT", &wxControl::Hide);
	}
	if (!allowCancel) {
		wxASSERT(allowForgotten);
		xrc_call(pwdDlg, "wxID_CANCEL", &wxControl::Disable);
	}

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			if (allowCancel) {
				return false;
			}
			continue;
		}

		bool const forgot = xrc_call(pwdDlg, "ID_FORGOT", &wxCheckBox::GetValue);
		if (!forgot) {
			auto pass = fz::to_utf8(xrc_call(pwdDlg, "ID_PASSWORD", &wxTextCtrl::GetValue));
			auto key = fz::private_key::from_password(pass, pub.salt_);

			if (key.pubkey() != pub) {
				wxMessageBoxEx(_("Wrong master password entered, it cannot be used to decrypt the stored passwords."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
			decryptors_[pub] = key;
		}
		else {
			decryptors_[pub] = fz::private_key();
		}
		break;
	}

	return true;
}

fz::private_key CLoginManager::GetDecryptor(fz::public_key const& pub)
{
	auto it = decryptors_.find(pub);
	if (it != decryptors_.cend()) {
		return it->second;
	}

	return fz::private_key();
}

void CLoginManager::Remember(const fz::private_key &key)
{
	decryptors_[key.pubkey()] = key;
}
