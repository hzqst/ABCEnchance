#include <plugins.h>

#include "httpclient.h"
#include <Controls.h>

#define MAX_COOKIE_LENGTH 2048

static HINTERFACEMODULE g_hUtilHTTPClient;
static IUtilHTTPClient* g_pUtilHTTPClient;
static CHttpClient g_pHttpClient;

void CHttpClient::Init(){
	g_hUtilHTTPClient = Sys_LoadModule("UtilHTTPClient_libcurl.dll");
	if (!g_hUtilHTTPClient){
		SYS_ERROR("Could not load UtilHTTPClient_libcurl.dll");
		return;
	}
	auto factory = Sys_GetFactory(g_hUtilHTTPClient);
	if (!factory){
		SYS_ERROR("Could not get factory from UtilHTTPClient_libcurl.dll");
		return;
	}
	g_pUtilHTTPClient = (decltype(g_pUtilHTTPClient))factory(UTIL_HTTPCLIENT_LIBCURL_INTERFACE_VERSION, NULL);
	if (!g_pUtilHTTPClient){
		SYS_ERROR("Could not get UtilHTTPClient from UtilHTTPClient_libcurl.dll");
		return;
	}
	if (g_pUtilHTTPClient){
		CUtilHTTPClientCreationContext context;
		context.m_bUseCookieContainer = true;
		context.m_bAllowResponseToModifyCookie = true;
		g_pUtilHTTPClient->Init(&context);
	}
}
void CHttpClient::RunFrame(){
	if (g_pUtilHTTPClient)
		g_pUtilHTTPClient->RunFrame();
	g_pHttpClient.CheckAll();
}
void CHttpClient::ShutDown(){
	if (g_pUtilHTTPClient){
		g_pUtilHTTPClient->Shutdown();
		g_pUtilHTTPClient->Destroy();
		g_pUtilHTTPClient = nullptr;
	}
	if (g_hUtilHTTPClient){
		Sys_FreeModule(g_hUtilHTTPClient);
		g_hUtilHTTPClient = nullptr;
	}
	g_pHttpClient.ClearAll();
}
void CHttpClient::CheckAll(){
	for (auto iter = m_aryItems.begin(); iter != m_aryItems.end();) {
		auto item = *iter;
		if (item->GetState() == HTTPCLIENT_STATE::DESTORYED) {
			delete item;
			iter = m_aryItems.erase(iter);
		}
		else
			iter++;
	}
}
void CHttpClient::ClearAll(){
	for (auto iter = m_aryItems.begin(); iter != m_aryItems.end(); iter++) {
		auto item = *iter;
		delete item;
	}
	m_aryItems.clear();
}
CHttpClientItem* CHttpClient::Fetch(const char* url, UtilHTTPMethod method){
	httpContext_t ctx = {
		url,
		method,
		nullptr
	};
	CHttpClientItem* item = new CHttpClientItem(&ctx);
	m_aryItems.push_back(item);
	return item;
}
CHttpClientItem* CHttpClient::Fetch(httpContext_s* ctx){
	CHttpClientItem* item = new CHttpClientItem(ctx);
	m_aryItems.push_back(item);
	return item;
}
bool CHttpClient::Interrupt(CHttpClientItem* pDestory){
	for (auto iter = m_aryItems.begin(); iter != m_aryItems.end();) {
		auto item = *iter;
		if (item == pDestory) {
			bool state = item->Interrupt();
			if (state) {
				delete item;
				iter = m_aryItems.erase(iter);
			}
			return state;
		}
		else
			iter++;
	}
	return false;
}

CHttpClientItem::CHttpClientItem(httpContext_s* ctx) : IUtilHTTPCallbacks(){
	m_hContext.url = ctx->url;
	m_hContext.method = ctx->method;
	m_pCookieJar = ctx->cookie;
	m_iStatue = HTTPCLIENT_STATE::INVALID;
}
CHttpClientItem* CHttpClientItem::Create(bool async){
	m_iStatue = HTTPCLIENT_STATE::PENDING;
	if (async)
		m_pRequest = g_pUtilHTTPClient->CreateAsyncRequest(m_hContext.url.c_str(), m_hContext.method, this);
	else
		m_pRequest = g_pUtilHTTPClient->CreateSyncRequest(m_hContext.url.c_str(), m_hContext.method, this);
	if (m_pCookieJar)
		m_pRequest->SetField("Set-Cookie", m_pCookieJar->Get().c_str());
	m_bAsync = async;
	return this;
}
CHttpClientItem* CHttpClientItem::Start(){
	if (m_bAsync) {
		if (m_pRequest)
			m_pRequest->Send();
	}
	return this;
}
IUtilHTTPResponse* CHttpClientItem::StartSync(){
	if (!m_bAsync) {
		m_pRequest->Send();
		m_pRequest->WaitForComplete();
		auto reb = m_pRequest->GetResponse(); 
		if (m_pCookieJar) {
			char cookiebuf[MAX_COOKIE_LENGTH];
			reb->GetHeader("Set-Cookie", cookiebuf, MAX_COOKIE_LENGTH);
			m_pCookieJar->Set(cookiebuf);
		}
		return reb;
	}
	return nullptr;
}
CHttpClientItem* CHttpClientItem::SetFeild(const char* key, const char* var){
	if (m_pRequest)
		m_pRequest->SetField(key, var);
	return this;
}

HTTPCLIENT_STATE CHttpClientItem::GetState() const{
	return m_iStatue;
}
bool CHttpClientItem::Interrupt(){
	m_pRequest->Destroy();
	return true;
}
void CHttpClientItem::Destroy() {
	m_iStatue = HTTPCLIENT_STATE::DESTORYED;
	if (m_pOnDestory)
		std::invoke(m_pOnDestory);
}
void CHttpClientItem::OnResponseComplete(IUtilHTTPRequest* RequestInstance, IUtilHTTPResponse* ResponseInstance) {
	if (!RequestInstance->IsRequestSuccessful()){
		m_iStatue = HTTPCLIENT_STATE::EXCEPTIONED;
		if (m_pOnFailed)
			std::invoke(m_pOnFailed, HTTPCLIENT_FAILED_CODE::REQUEST_ERROR);
		return;
	}
	if (ResponseInstance->IsResponseError()){
		m_iStatue = HTTPCLIENT_STATE::EXCEPTIONED;
		if (m_pOnFailed)
			std::invoke(m_pOnFailed, HTTPCLIENT_FAILED_CODE::RESPONSE_ERROR);
		return;
	}
	m_iStatue = HTTPCLIENT_STATE::RESPONDED;
	if (m_pOnResponse) {
		std::invoke(m_pOnResponse, ResponseInstance);
		if (m_pCookieJar) {
			char cookiebuf[MAX_COOKIE_LENGTH];
			ResponseInstance->GetHeader("Set-Cookie", cookiebuf, MAX_COOKIE_LENGTH);
			m_pCookieJar->Set(cookiebuf);
		}
	}
}
void CHttpClientItem::OnUpdateState(UtilHTTPRequestState NewState) {
	switch (NewState)
	{
	case UtilHTTPRequestState::Idle:
		break;
	case UtilHTTPRequestState::Requesting:
		break;
	case UtilHTTPRequestState::Responding:
		break;
	case UtilHTTPRequestState::Finished: {
		m_iStatue = HTTPCLIENT_STATE::FINISHED;
		if (m_pOnFinish)
			std::invoke(m_pOnFinish);
		break;
	}
	default:
		break;
	}
}

void CHttpClientItem::OnReceiveData(IUtilHTTPRequest* RequestInstance, IUtilHTTPResponse* ResponseInstance, const void* pData, size_t cbSize)
{
	m_aryReciveData.resize(cbSize);
	std::memcpy(m_aryReciveData.data(), pData, cbSize);
}

CHttpClient* GetHttpClient(){
	return &g_pHttpClient;
}

CHttpCookieJar::CHttpCookieJar(){
}
CHttpCookieJar::CHttpCookieJar(const char* path){
	Load(path);
}
void CHttpCookieJar::Load(const char* path){
	FileHandle_t file = vgui::filesystem()->Open(path, "r");
	if (file) {
		char buffer[MAX_COOKIE_LENGTH];
		vgui::filesystem()->Read(buffer, MAX_COOKIE_LENGTH, file);
		m_szCookie = buffer;
		m_szCookie += '\0';
		m_szPath = path;
	}
	vgui::filesystem()->Close(file);
}
void CHttpCookieJar::Save(){
	if (m_szPath.empty())
		return;
	FileHandle_t file = vgui::filesystem()->Open(m_szPath.c_str(), "w");
	if (file)
		vgui::filesystem()->Write(m_szCookie.c_str(), m_szCookie.size(), file);
	vgui::filesystem()->Close(file);
}
std::string CHttpCookieJar::Get(){
	return m_szCookie;
}
void CHttpCookieJar::Set(const char* cookie){
	m_szCookie = cookie;
}

size_t CHttpCookieJar::Size(){
	return m_szCookie.size();
}
