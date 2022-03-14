
#include "stdafx.h"
#include "PharmaRobot 1.0.h"
#include "PharmaRobot 1.0Dlg.h"
#include "afxdialogex.h"
#include <afxsock.h>    // For CSocket 
#include <iostream>
#include "Iphlpapi.h"
#include "ProRBT.h"
#include "ConsisComm.h"

extern 	PRORBTPARAMSACK ackemessage;

CPharmaRobot10Dlg* g_pdialog;

ProRbtDb g_ProRbtDb;

QUERYRESPONSE HandleQueryCommand(PRORBTPARAMS * pProRbtParams, CPharmaRobot10Dlg* pdialog)
{
	size_t retsize;
	BConsisStockRequest * pBresponse = (BConsisStockRequest *)pdialog->ConsisMessageB;

	if (pdialog->Consis.ConnectionStarted == FALSE)
	{
		if (pdialog->Consis.ConnectToConsis("ShorT", &(pdialog->m_listBoxMain), &(pdialog->m_CheckBoxRemoteSvr)))
			pdialog->EnableCondsisTab();
	}

	//Try to catch the Mutex for CONSIS Access
	//Protect with Mutex the CONSIS resource
	CSingleLock singleLock(&(pdialog->m_MutexBMessage));

	// Attempt to lock the shared resource
	if (singleLock.Lock(INFINITE))
	{
		//log locking success
	}

	if (pdialog->Consis.ConnectionStarted == TRUE) 
	{//Build B Message with Barcode from RBT parameters
		memset(pdialog->ConsisMessageB, '0', 41);
		pdialog->ConsisMessageB[41] = '\0';

		/*Counter Unit*/
		CString CounterUnitString = pProRbtParams->CounterUnit;
		size_t len = CounterUnitString.GetLength();
		int location = 4 - len;
		wchar_t Source[4];
		wsprintf(Source, CounterUnitString.GetString());
		wcstombs((&(pdialog->ConsisMessageB[location])), Source, len);

		/*Barcode*/
		CString BarCodeString = pProRbtParams->Barcode;
		len = BarCodeString.GetLength();
		location = 41 - len;
		wchar_t barcode[14];
		wsprintf(barcode, BarCodeString.GetString());

		memset(pBresponse->ArticleId,' ',30); //Clear leading zeros

		wcstombs(&(pdialog->ConsisMessageB[location]), barcode, len);

		pdialog->ConsisMessageB[0] = 'B';

		/* Send B message to CONSIS */
		pdialog->Consis.SendConsisMessage(pdialog->ConsisMessageB, 42);

		memset(pdialog->Consis.bmessageBuffer, 0, MAX_CONSIS_MESSAGE_SIZE);

		/* Infinitly wait for the reply to arrive from CONSIS by means of AsynchDialogue listener*/
		::WaitForSingleObject(pdialog->Consis.bMessageEvent.m_hObject, INFINITE);

		bConsisReplyHeader *pHeader = (bConsisReplyHeader *)pdialog->Consis.bmessageBuffer;

		//Extract number of locations
		char numloc[3];
		memcpy(numloc, pHeader->NumStockLocations, sizeof(pHeader->NumStockLocations));
		numloc[2] = '\0';
		int numLocation =  atoi(numloc);

		//Extract Total Quantity of Item
		char TotalQua[6];
		TotalQua[5] = '\0';
		memcpy(TotalQua, pHeader->TotalQuantity, sizeof(pHeader->TotalQuantity));
		int totalQuantity =  atoi(TotalQua);

		//Find Article ID which is in 'b' Footer after article locations
		char* address = (char*)pHeader + sizeof(bConsisReplyHeader) + (numLocation * (sizeof(bConsisReplyStockLocations)));
		bConsisReplyFooter* bfooter = (bConsisReplyFooter*)address;

		//Extract Article ID
		wchar_t articleID[31];
		articleID[30] = '\0';
		mbstowcs_s(&retsize, articleID, sizeof(bfooter->ArticleId) + 1, bfooter->ArticleId, _TRUNCATE);

		//Clean leading zeroes
		CString cleanArticleID;
		cleanArticleID.SetString(articleID);
		cleanArticleID.TrimLeft(L' ');
		wsprintf(articleID,cleanArticleID.GetString());

		wchar_t description[256];
		//Get Description from Yarpa SQL
		if (pdialog->GetItemDescFromBarcode(articleID, description))
		{
			if (totalQuantity)
			{
				wsprintf(ackemessage.Message,L"מצב מלאי\nסוג: %s\nמספר במלאי [%d]", description, totalQuantity);
			}
			else
			{
				wsprintf(ackemessage.Message,L"אין פריטים במלאי מסוג \n%s", description);
			}
		}
		else
		{
			//Fill Ack message content
			if (totalQuantity)
			{
				wsprintf(ackemessage.Message,L"מפריט בעל מספר מזהה\n%s\nקיימים %d\nבמלאי", cleanArticleID.GetString(), totalQuantity);
			}
			else
			{
				wsprintf(ackemessage.Message,L"פריט בעל מספר מזהה\n%s\nאינו קיים במלאי", cleanArticleID.GetString());
			}
		}

		singleLock.Unlock();
		return Q_SENDACK;
	}
	wsprintf(ackemessage.Message,L" שרת קונסיס לא זמין\0");
	singleLock.Unlock();
	return Q_SENDACK;
}

DWORD WINAPI ClientSocketHandlerThread(SOCKET handle)
{
	QUERYRESPONSE res;
	SOCKADDR_IN echoClntAddr;        // Client address
	int clntLen;                     // Length of client address data structure 
	char echoBuffer[sizeof(PRORBTPARAMS)]; // Buffer for echo string
	PRORBTPARAMS * pProRoboParams = (PRORBTPARAMS *)echoBuffer;
	// Get the size of the in-out parameter
	clntLen = sizeof(echoClntAddr);

	CSocket clntSock;
	
	clntSock.Attach(handle);

	// Get the client's host name
	if (!clntSock.GetPeerName((SOCKADDR*)&echoClntAddr, &clntLen)) {

	}

	int recvMsgSize;              // Size of received message

	// Recieve message from client 
	recvMsgSize = clntSock.Receive(echoBuffer, sizeof(PRORBTPARAMS), 0);
	if (recvMsgSize < 0) {

	}

	CString st;
	if (pProRoboParams->Header[0] == '`')
	{
		g_pdialog->m_listBoxMain.ResetContent();
		//st = "Received from Client: "; st += clientaddress; pdialog->m_listBoxMain.AddString(st);
		st = "Counter Unit ID: "; st +=  pProRoboParams->CounterUnit; g_pdialog->m_listBoxMain.AddString(st);
		st = "Directive: "; st += pProRoboParams->Directive; g_pdialog->m_listBoxMain.AddString(st);
		st = "Bracode: "; st += pProRoboParams->Barcode; g_pdialog->m_listBoxMain.AddString(st);
		st = "Qty: "; st += pProRoboParams->Qty; g_pdialog->m_listBoxMain.AddString(st);
		st = "SessionId: "; st += pProRoboParams->SessionId; g_pdialog->m_listBoxMain.AddString(st);
		st = "LineNum: "; st += pProRoboParams->LineNum; g_pdialog->m_listBoxMain.AddString(st);
		st = "TotalLines: "; st += pProRoboParams->TotalLines; g_pdialog->m_listBoxMain.AddString(st);

		if (pProRoboParams->Directive[0] == L'1')
		{
			res = HandleQueryCommand(pProRoboParams, g_pdialog);
		}
		else if (pProRoboParams->Directive[0] == L'2')
		{
			res = g_ProRbtDb.HandleProRbtLine(pProRoboParams, g_pdialog);
		}
		switch (res)
		{
		case Q_ERROR:
			// Echo message back to client
			ackemessage.Header[0] = L'`';
			ackemessage.Type[0] = L'1';
			clntSock.Send((wchar_t*)ackemessage.Header, sizeof(ackemessage), 0);
			st.SetString(L"Ack Sent"); g_pdialog->m_listBoxMain.AddString(st);
			break;

		case Q_NOACK:
			ackemessage.Header[0] = L'`';
			ackemessage.Type[0] = L'0';
			clntSock.Send((wchar_t*)ackemessage.Header, sizeof(ackemessage), 0);
			break;

		case Q_SENDACK:
			// Echo message back to client
			ackemessage.Header[0] = L'`';
			ackemessage.Type[0] = L'1';
			clntSock.Send((wchar_t*)ackemessage.Header, sizeof(ackemessage), 0);
			st.SetString(L"Ack Sent"); g_pdialog->m_listBoxMain.AddString(st);
			break ;
		}

	}
	else
	{
		st.SetString(L"Bad Packet Content"); g_pdialog->m_listBoxMain.AddString(st);
	}

	clntSock.Close();

	return 0;
}



DWORD WINAPI SocketThread(CPharmaRobot10Dlg* pdialog)
{

	HANDLE hSocketThread;
	
	g_pdialog = pdialog;
	
	// Initialize the AfxSocket
	AfxSocketInit(NULL);

	int echoServPort = 50004;  // First arg: local port
	CSocket servSock;                  // Socket descriptor for server

	// Create the server socket
	if (!servSock.Create(echoServPort)) {
		//DieWithError("servSock.Create() failed");
	}

	// Mark the socket so it will listen for incoming connections
	if (!servSock.Listen(5)) {
		//DieWithError("servSock.Listen() failed");
	}

	for(;;) { // Run forever

		if (pdialog->ExitThreads == TRUE)
			break;
		
		//create the socket
		CSocket clntSock;

		// Wait for a client to connect
		if (!servSock.Accept(clntSock)) {
			//DieWithError("servSock.Accept() failed");
		}

		//Detach the socket so the dedicated client thread may access it
		SOCKET SockeHandler = clntSock.Detach();

		// ClntSock is connected to a client!
		hSocketThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ClientSocketHandlerThread, (LPVOID)SockeHandler, 0, NULL);

		CloseHandle(hSocketThread);

	}

  // NOT REACHED

  return 0;
}