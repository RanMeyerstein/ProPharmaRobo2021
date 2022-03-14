/*
Handles 'P' Product Information Request from CONSIS, queries Yarpa SQL database for product description
and send back a 'p' response to CONSIS.
*/

#include "stdafx.h"
#include "ConsisComm.h"
#include "PharmaRobot 1.0.h"
#include "PharmaRobot 1.0Dlg.h"

void CleanString(wchar_t* stringptr, size_t length)
{
	wchar_t helper[256];
	memset (helper , 0, 256);
	int numberdeleted = 0;

	if (length > 39)
		return;

	for (UINT32 i = 0; i < length; i++)
	{
		if (stringptr [i] == 0)
		{
			break;
		}
		if (stringptr [i] > 8000)
		{
			memcpy(&helper[i - numberdeleted], &stringptr[i + 1], 40);
			numberdeleted++;
		}
	}
	if (numberdeleted != 0)
	{//There's a new helper conetnt
		memcpy(stringptr, helper, 39);
	}
}


/* Low priority task checks if there's a message to be received from CONSIS and then sleeps for 200 msec */
DWORD WINAPI AsynchDialogueListenerThread(CPharmaRobot10Dlg* pdialog)
{
	int MessageLength, MessagePSize = 114, MessageISize = 67, lisRobotItem;
	char buffer[MAX_CONSIS_MESSAGE_SIZE];
	size_t retsize,	convertedChars = 0;;
	pConsisRequestMessage * ppRequestMessage;
	PConsisPmsResponseMessage PResponseToConsis;
	iConsisRequestMessage * piRequestMessage;
	IConsisPmsResponseMessage IResponseToConsis;
	wchar_t description[256], wcstring[256];

	for (;;) //Run forever
	{
		if (pdialog->ExitThreads == TRUE)
			break;

		//Perform Message checking only if CONSIS is connected
		if (pdialog->Consis.ConnectionStarted == TRUE)
		{

			//clear buffer
			memset(buffer, 0, MAX_CONSIS_MESSAGE_SIZE);

			MessageLength = sizeof(buffer);
			pdialog->Consis.ReceiveConsisMessage(buffer, &MessageLength, 0 ); //no timeout

			//Check if we received a 'p' request and handle it
			if (buffer[0] == 'p')
			{//handle a 'p' message here
				mbstowcs_s(&convertedChars, wcstring, sizeof(pConsisRequestMessage) + 1, buffer, _TRUNCATE);
				wcstring[sizeof(pConsisRequestMessage)] = L'\0';
				pdialog->m_listBoxMain.AddString(wcstring);

				ppRequestMessage = (pConsisRequestMessage *)buffer;
				//Extract Article ID
				wchar_t articleID[31];
				articleID[30] = '\0';
				mbstowcs_s(&retsize, articleID, sizeof(ppRequestMessage->ArticleId) + 1, ppRequestMessage->ArticleId, _TRUNCATE);

				//Clean leading zeroes
				CString cleanArticleID;
				cleanArticleID.SetString(articleID);
				cleanArticleID.TrimLeft(L' ');
				cleanArticleID.TrimLeft(L'0');
				wsprintf(articleID,cleanArticleID.GetString());

				//clean the message to CONSIS
				memset ((void*)&PResponseToConsis, '0',sizeof(PResponseToConsis));
				memset (&(PResponseToConsis.ArticleName),' ',sizeof(PResponseToConsis.ArticleName));

				//Get Description from Yarpa SQL
				if (pdialog->GetItemDescFromBarcode(articleID, description))
				{//Got a description
					//Clean the description from ascii codes 8206
					CleanString(description, 39);
					size_t sizeloc = 39, sizesent = 39;
					char DescriptionInChar[40];
					memset(DescriptionInChar, ' ', 39);
					DescriptionInChar[39] = 0;
					wcstombs_s(&sizeloc, DescriptionInChar, description, _TRUNCATE);
					sprintf_s(PResponseToConsis.ArticleName, DescriptionInChar);
				}
				else
				{
					sprintf_s(PResponseToConsis.ArticleName, "Name Missing");
				}
				PResponseToConsis.RecordType = 'P';
				memcpy(&(PResponseToConsis.ArticleId), ppRequestMessage->ArticleId, sizeof(PResponseToConsis.ArticleId));

				mbstowcs_s(&convertedChars, wcstring, sizeof(PResponseToConsis) + 1, (char*)&PResponseToConsis, _TRUNCATE);
				wcstring[sizeof(PResponseToConsis)] = L'\0';
				pdialog->m_listBoxMain.AddString(wcstring);
				//Send a response to CONSIS
				pdialog->Consis.SendConsisMessage((char*)&PResponseToConsis, MessagePSize);
			}

			//Check if we received an 'i' request and handle it
			if (buffer[0] == 'i')
			{//handle an 'i' message here
				mbstowcs_s(&convertedChars, wcstring, sizeof(iConsisRequestMessage) + 1, buffer, _TRUNCATE);
				wcstring[sizeof(iConsisRequestMessage)] = L'\0';
				pdialog->m_listBoxMain.AddString(wcstring);

				piRequestMessage = (iConsisRequestMessage *)buffer;

				//Extract Article ID
				wchar_t articleID[31];
				articleID[30] = '\0';
				mbstowcs_s(&retsize, articleID, sizeof(piRequestMessage->ArticleId) + 1, piRequestMessage->ArticleId, _TRUNCATE);

				//Clean leading zeroes
				CString cleanArticleID;
				cleanArticleID.SetString(articleID);
				cleanArticleID.TrimLeft(L'0');
				cleanArticleID.TrimLeft(L' ');
				wsprintf(articleID,cleanArticleID.GetString());

				//clean the message to CONSIS
				//State will be set to '00' by this action
				memset ((void*)&IResponseToConsis, '0',sizeof(IResponseToConsis));

				//check if message is '04-New article (return) '05-New article (goods receipt) type.
				if ((piRequestMessage->OrderState[1] == '4') || (piRequestMessage->OrderState[1] == '5'))
				{
					//Check if TA is 999 in Yarpa DB for this Barcode number
					if (pdialog->GetTaFromYarpaByBarcode(articleID, &lisRobotItem))
					{//Got a TA from SQL
						if(lisRobotItem != 999)
							IResponseToConsis.State[1]='1';//Article may not be stored as its not a ROBOT item according to YARPA DB
					}
					else
					{
						IResponseToConsis.State[1]='1';//Article may not be stored as not found in YARPA DB
					}
				}

				IResponseToConsis.RecordType = 'I';
				//Get Article ID from request
				memset((char*)(&(IResponseToConsis.ArticleId[0])), ' ', 30);
				memcpy(&(IResponseToConsis.ArticleId), piRequestMessage->ArticleId, sizeof(IResponseToConsis.ArticleId));

				//Get Order number from request
				memcpy(&(IResponseToConsis.OrderNumber), piRequestMessage->OrderNumber, sizeof(IResponseToConsis.OrderNumber));

				//Get Demanding Counetr ID from request
				memcpy(&(IResponseToConsis.DemandingCounterUnitId), piRequestMessage->DemandingCounterUnitId, 
					sizeof(IResponseToConsis.DemandingCounterUnitId));

				IResponseToConsis.Text[3] = ' ';
				
				mbstowcs_s(&convertedChars, wcstring, sizeof(IResponseToConsis) + 1, (char*)&IResponseToConsis, _TRUNCATE);
				wcstring[sizeof(IResponseToConsis)] = L'\0';
				pdialog->m_listBoxMain.AddString(wcstring);
				//Send a response to CONSIS
				pdialog->Consis.SendConsisMessage((char*)&IResponseToConsis, MessageISize);
			}

			//Check if we received an 'b' request, copy content into 'b' buffer and set event for waiting thread
			if (buffer[0] == 'b')
			{
				memcpy(pdialog->Consis.bmessageBuffer, buffer, MessageLength);
				pdialog->Consis.bMessageLength = MessageLength;
				pdialog->Consis.bMessageEvent.SetEvent();
			}
			//Check if we received an 'a' request, copy content into 'a' buffer and set event for waiting thread
			if (buffer[0] == 'a')
			{
				memcpy(pdialog->Consis.amessageBuffer, buffer, MessageLength);
				pdialog->Consis.aMessageLength = MessageLength;
				pdialog->Consis.aMessageEvent.SetEvent();
			}
		
		}//CONSIS connected if statement context

		Sleep(20);
	}
	// NOT REACHED

	return 0;
}
