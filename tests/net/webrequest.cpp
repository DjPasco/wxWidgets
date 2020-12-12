/////////////////////////////////////////////////////////////////////////////
// Name:        tests/net/webrequest.cpp
// Purpose:     wxWebRequest test
// Author:      Tobias Taschner
// Created:     2018-10-24
// Copyright:   (c) 2018 wxWidgets development team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "testprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif // WX_PRECOMP

#include "wx/webrequest.h"
#include "wx/filename.h"
#include "wx/wfstream.h"

// This test requires the URL to an httpbin instance.
// httpbin is a HTTP Request & Response Service available at:
// https://httpbin.org
// It can also be run locally via a simple docker run
//
// For this test to run the the base URL has to be specified with
// an environment variable, e.g.:
//   WX_TEST_WEBREQUEST_URL=https://httpbin.org

#if wxUSE_WEBREQUEST

class RequestFixture
{
public:
    RequestFixture()
    {
        expectedFileSize = 0;
        dataSize = 0;
    }

    void Create(const wxString& subURL)
    {
        wxString baseURL;
        wxGetEnv("WX_TEST_WEBREQUEST_URL", &baseURL);
        CreateAbs(baseURL + subURL);
    }

    void CreateAbs(const wxString& url)
    {
        request.reset(wxWebSession::GetDefault().CreateRequest(url));
        request->Bind(wxEVT_WEBREQUEST_STATE, &RequestFixture::OnRequestState, this);
        request->Bind(wxEVT_WEBREQUEST_DATA, &RequestFixture::OnData, this);
    }

    void OnRequestState(wxWebRequestEvent& evt)
    {
        switch (evt.GetState())
        {
        case wxWebRequest::State_Unauthorized:
        case wxWebRequest::State_Completed:
            if ( request->GetStorage() == wxWebRequest::Storage_File )
            {
                wxFileName fn(evt.GetResponseFileName());
                REQUIRE( fn.GetSize() == expectedFileSize );
            }
            wxFALLTHROUGH;
        case wxWebRequest::State_Failed:
        case wxWebRequest::State_Cancelled:
            loop.Exit();
            break;

        case wxWebRequest::State_Idle:
        case wxWebRequest::State_Active:
            break;
        }
    }

    void OnData(wxWebRequestEvent& evt)
    {
        // Count all bytes recieved via data event for Storage_None
        dataSize += evt.GetDataSize();
    }

    void Run(wxWebRequest::State requiredState = wxWebRequest::State_Completed,
        int requiredStatus = 200)
    {
        REQUIRE( request->GetState() == wxWebRequest::State_Idle );
        request->Start();
        loop.Run();
        REQUIRE( request->GetState() == requiredState );
        if (requiredStatus)
            REQUIRE( request->GetResponse()->GetStatus() == requiredStatus );
    }

    wxEventLoop loop;
    wxObjectDataPtr<wxWebRequest> request;
    wxInt64 expectedFileSize;
    wxInt64 dataSize;
};

TEST_CASE_METHOD(RequestFixture, "WebRequest", "[net][.]")
{
    wxString baseURL;
    if ( !wxGetEnv("WX_TEST_WEBREQUEST_URL", &baseURL) )
    {
        WARN("Skipping WebRequest test because required WX_TEST_WEBREQUEST_URL"
            " environment variable is not defined.");
        return;
    }

    SECTION("GET 64kb to memory")
    {
        Create("/bytes/65536");
        Run();
        REQUIRE( request->GetResponse()->GetContentLength() == 65536 );
        REQUIRE( request->GetBytesExpectedToReceive() == 65536 );
        REQUIRE( request->GetBytesReceived() == 65536 );
    }

    SECTION("GET 404 error")
    {
        Create("/status/404");
        Run(wxWebRequest::State_Failed, 404);
    }

    SECTION("Connect to invalid host")
    {
        CreateAbs("http://127.0.0.1:51234");
        Run(wxWebRequest::State_Failed, 0);
    }

    SECTION("POST form data")
    {
        Create("/post");
        request->SetData("app=WebRequestSample&version=1", "application/x-www-form-urlencoded");
        Run();
    }

    SECTION("GET data as string")
    {
        Create("/base64/VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZw==");
        Run();
        REQUIRE( request->GetResponse()->AsString() == "The quick brown fox jumps over the lazy dog" );
    }

    SECTION("GET 99KB to file")
    {
        expectedFileSize = 99 * 1024;
        Create(wxString::Format("/bytes/%lld", expectedFileSize));
        request->SetStorage(wxWebRequest::Storage_File);
        Run();
        REQUIRE( request->GetBytesReceived() == expectedFileSize );
    }

    SECTION("Process 99KB data")
    {
        int processingSize = 99 * 1024;
        Create(wxString::Format("/bytes/%d", processingSize));
        request->SetStorage(wxWebRequest::Storage_None);
        Run();
        REQUIRE( request->GetBytesReceived() == processingSize );
        REQUIRE( dataSize == processingSize );
    }

    SECTION("PUT file data")
    {
        Create("/put");
        wxSharedPtr<wxInputStream> is(new wxFileInputStream("horse.png"));
        REQUIRE( is->IsOk() );

        request->SetData(is, "image/png");
        request->SetMethod("PUT");
        Run();
    }

    SECTION("Server auth BASIC")
    {
        Create("/digest-auth/auth/wxtest/wxwidgets");
        Run(wxWebRequest::State_Unauthorized, 401);
        REQUIRE( request->GetAuthChallenge() != NULL );
        request->GetAuthChallenge()->SetCredentials("wxtest", "wxwidgets");
        loop.Run();
        REQUIRE( request->GetResponse()->GetStatus() == 200 );
        REQUIRE( request->GetState() == wxWebRequest::State_Completed );
    }

    SECTION("Server auth DIGEST")
    {
        Create("/digest-auth/auth/wxtest/wxwidgets");
        Run(wxWebRequest::State_Unauthorized, 401);
        REQUIRE( request->GetAuthChallenge() != NULL );
        request->GetAuthChallenge()->SetCredentials("wxtest", "wxwidgets");
        loop.Run();
        REQUIRE( request->GetResponse()->GetStatus() == 200 );
        REQUIRE( request->GetState() == wxWebRequest::State_Completed );
    }
}

TEST_CASE("WebRequestUtils", "[net]")
{
    wxString value;
    wxWebRequestHeaderMap params;

    wxString header = "multipart/mixed; boundary=\"MIME_boundary_01234567\"";

    wxWebRequest::SplitParameters(header, value, params);
    REQUIRE( value == "multipart/mixed" );
    REQUIRE( params.size() == 1 );
    REQUIRE( params["boundary"] == "MIME_boundary_01234567" );
}

#endif // wxUSE_WEBREQUEST
