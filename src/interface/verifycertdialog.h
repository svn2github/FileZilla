#ifndef FILEZILLA_INTERFACE_VERIFYCERTDIALOG_HEADER
#define FILEZILLA_INTERFACE_VERIFYCERTDIALOG_HEADER

#include "xmlfunctions.h"

#include <set>

class CertStore final
{
public:
	CertStore();

	bool IsTrusted(CCertificateNotification const& notification);
	void SetTrusted(CCertificateNotification const& notification, bool permanent, bool trustAllHostnames);

	void SetInsecure(std::wstring const& host, unsigned int port, bool permanent);

	bool IsInsecure(std::wstring const& host, unsigned int port, bool permanentOnly = false);

	bool HasCertificate(std::wstring const& host, unsigned int port);

private:
	struct t_certData {
		std::wstring host;
		bool trustSans{};
		unsigned int port{};
		std::vector<uint8_t> data;
	};

	bool IsTrusted(std::wstring const& host, unsigned int port, std::vector<uint8_t> const& data, bool permanentOnly, bool allowSans);
	bool DoIsTrusted(std::wstring const& host, unsigned int port, std::vector<uint8_t> const& data, std::list<t_certData> const& trustedCerts, bool allowSans);

	void LoadTrustedCerts();

	std::list<t_certData> trustedCerts_;
	std::list<t_certData> sessionTrustedCerts_;
	std::set<std::tuple<std::wstring, unsigned int>> insecureHosts_;
	std::set<std::tuple<std::wstring, unsigned int>> sessionInsecureHosts_;

	CXmlFile m_xmlFile;
};

class wxDialogEx;
class CVerifyCertDialog final : protected wxEvtHandler
{
public:
	CVerifyCertDialog(CertStore & certStore);

	void ShowVerificationDialog(CCertificateNotification& notification, bool displayOnly = false);

private:

	bool DisplayAlgorithm(int controlId, wxString name, bool insecure);

	bool DisplayCert(wxDialogEx* pDlg, const CCertificate& cert);

	void ParseDN(wxWindow* parent, const wxString& dn, wxSizer* pSizer);
	void ParseDN_by_prefix(wxWindow* parent, std::list<wxString>& tokens, wxString prefix, const wxString& name, wxSizer* pSizer, bool decode = false);

	wxString DecodeValue(const wxString& value);

	std::vector<CCertificate> m_certificates;
	wxDialogEx* m_pDlg{};
	wxSizer* m_pSubjectSizer{};
	wxSizer* m_pIssuerSizer{};
	int line_height_{};

	void OnCertificateChoice(wxCommandEvent& event);

	CertStore & certStore_;
};

void ConfirmInsecureConection(CertStore & certStore, CInsecureFTPNotification & notification);

#endif
