#include "fat32.h"
#include <shlwapi.h>
#include <windows.h>
#include <queue>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "commutil.h"

#pragma comment(lib,"shlwapi.lib")

using namespace std;

CFat32FileSystem::CFat32FileSystem(IBaseReader	*prmReader)
	:CBaseFileSystem(prmReader), m_clusterFlag(NULL)
{
	m_fatTable = NULL;
}

CFat32FileSystem::~CFat32FileSystem()
{
	free(m_fatTable);
	free(m_clusterFlag);
}

void CFat32FileSystem::Init()
{
	//��ȡfat32ͷ������
	this->ReadBuf((UCHAR*)&m_fatSector,0,512);

	m_fatNum = m_fatSector.BPB_FATSz32 * m_fatSector.BPB_BytsPerSec / 4;
	m_fatTable = (UINT32*)malloc(sizeof(UINT32)*m_fatNum);
	memset(m_fatTable, 0, sizeof(UINT32)*m_fatNum);
	if (m_fatTable == NULL)
	{
		return;
	}
	m_clusterFlag = (UCHAR *)malloc(sizeof(UCHAR)*m_fatNum);
	if (m_clusterFlag == NULL)
	{
		return;
	}
	memset(m_clusterFlag, 0, m_fatNum);
	//��ȡ�ļ������
	this->ReadBuf((UCHAR*)m_fatTable, m_fatSector.BPB_ResvdSecCnt, sizeof(UINT32)*m_fatNum);
	m_bytesPerSector = m_fatSector.BPB_BytsPerSec;
	m_sectorsPerCluster = m_fatSector.BPB_SecPerClus;

	//��ȡfat32��Ŀ¼��Ϣ
	m_rootDirectory = new CBaseFileObject;
	m_rootDirectory->SetFileType(FILE_OBJECT_TYPE_ROOT);
	m_rootDirectory->SetFileSize(0);
	m_rootDirectory->SetFileName(TEXT(""));
	UINT64	tmpRootStartSector = m_fatSector.BPB_FATSz32 * m_fatSector.BPB_NumFATs;
	tmpRootStartSector += m_fatSector.BPB_ResvdSecCnt;
	tmpRootStartSector += (2-m_fatSector.BPB_RootClus)*m_fatSector.BPB_SecPerClus;
	m_rootDirectory->SetFileStartSector(tmpRootStartSector);
}

UINT64	CFat32FileSystem::ReadFileContent(CBaseFileObject *prmFileObject, UCHAR prmDstBuf[], UINT64 prmByteOff, UINT64 prmByteToRead)
{
	if (prmFileObject->GetFileType() != FILE_OBJECT_TYPE_FILE)
	{
		return 0;
	}
	UINT64		tmpFileSize = prmFileObject->GetFileSize();
	if (tmpFileSize >= 0xFFFFFFFF)
	{
		return 0;
	}
	if (prmByteOff >= tmpFileSize)
	{
		return 0;
	}
	UINT32		tmpRemainSize = (UINT32)tmpFileSize - (UINT32)prmByteOff;
	if (tmpRemainSize<prmByteToRead)
	{
		prmByteToRead = tmpRemainSize;
	}
	UINT32		tmpClusterSize = m_fatSector.BPB_SecPerClus*m_fatSector.BPB_BytsPerSec;
	UINT32		tmpClusterNum = (UINT32)prmFileObject->GetFileStartSector();
	//���ݷ����������ż���غ�
	tmpClusterNum -= m_fatSector.BPB_FATSz32*m_fatSector.BPB_NumFATs;
	tmpClusterNum -= m_fatSector.BPB_ResvdSecCnt;
	tmpClusterNum = tmpClusterNum / m_fatSector.BPB_SecPerClus + 2;

	while (prmByteOff >= tmpClusterSize && tmpClusterNum)
	{
		prmByteOff -= tmpClusterSize;
		tmpClusterNum = this->GetNextCluster(tmpClusterNum);
	}
	if (tmpClusterNum == 0)
	{
		//�Ƿ�����
		return 0;
	}
	char	*tmpBuf = (char*)malloc(tmpClusterSize);
	if (tmpBuf == NULL)
	{
		return 0;
	}
	UINT32 tmpOffset = m_fatSector.BPB_FATSz32*m_fatSector.BPB_NumFATs;
	tmpOffset += m_fatSector.BPB_ResvdSecCnt;
	tmpOffset += (tmpClusterNum - m_fatSector.BPB_RootClus)*m_fatSector.BPB_SecPerClus;
	//��ȡprmByteOff����fat32�����еĴ�����
	UINT64 tmpRet = this->ReadBuf((UCHAR*)tmpBuf, tmpOffset, tmpClusterSize);
	UINT64		tmpResult = 0;
	if (prmByteToRead <= tmpClusterSize - prmByteOff)
	{
		//��ȡ������С��һ�ش�С
		memcpy(prmDstBuf, tmpBuf + (UINT32)prmByteOff, (UINT32)prmByteToRead);
		tmpResult = prmByteToRead;
		goto SUCCESS;
	}
	else
	{
		//��һ�����������ݷ��뻺����
		memcpy(prmDstBuf, tmpBuf + (UINT32)prmByteOff, tmpClusterSize - (UINT32)prmByteOff);
		tmpResult = tmpClusterSize - prmByteOff;
		tmpClusterNum = this->GetNextCluster(tmpClusterNum);
		//���ٴ���ȡ���ݴ�С
		prmByteToRead = prmByteToRead - tmpResult;
	}
	//ʣ�µ��������ض�ȡ
	for (int i = 0; i<prmByteToRead / tmpClusterSize; i++)
	{
		tmpOffset = m_fatSector.BPB_FATSz32*m_fatSector.BPB_NumFATs;
		tmpOffset += m_fatSector.BPB_ResvdSecCnt;
		tmpOffset += (tmpClusterNum - m_fatSector.BPB_RootClus)*m_fatSector.BPB_SecPerClus;
		this->ReadBuf(prmDstBuf + tmpResult, tmpOffset, tmpClusterSize);
		tmpResult += tmpClusterSize;
		prmByteToRead = prmByteToRead - tmpClusterSize;
		tmpClusterNum = this->GetNextCluster(tmpClusterNum);
	}
	//���ǿ��ܻ���������Ҫ��ȡ��������һ�ش�С
	if (prmByteToRead>0)
	{
		tmpOffset = m_fatSector.BPB_FATSz32*m_fatSector.BPB_NumFATs;
		tmpOffset += m_fatSector.BPB_ResvdSecCnt;
		tmpOffset += (tmpClusterNum - m_fatSector.BPB_RootClus)*m_fatSector.BPB_SecPerClus;
		this->ReadBuf(prmDstBuf + tmpResult, tmpOffset, prmByteToRead);
		tmpResult += prmByteToRead;
	}
SUCCESS:
	free(tmpBuf);
	return tmpResult;
}

void CFat32FileSystem::GetDeletedFiles(vector<CBaseFileObject *> &fileArray)
{
	TCHAR	fileName[MAX_PATH] = { 0 };
	//FileInfo	fileInfo;
	UINT32	clusNum = 0;
	UINT64	offset = 0;
	queue<UINT32> dirs;
	//this->FreeArray(fileArray);
	dirs.push(2);//���Ȱ�root�Ĵغŷŵ�������

	UINT32	clusterSize = m_fatSector.BPB_BytsPerSec*m_fatSector.BPB_SecPerClus;
	UCHAR	*szBuf = (UCHAR*)malloc(sizeof(UCHAR)*clusterSize);
	while (!dirs.empty())
	{
		clusNum = dirs.front();
		dirs.pop();
		//�����ļ��еĵڸ���
		while (1)
		{
			offset = m_fatSector.BPB_FATSz32 * m_fatSector.BPB_NumFATs;
			offset += m_fatSector.BPB_ResvdSecCnt;
			offset += (clusNum - m_fatSector.BPB_RootClus)*m_fatSector.BPB_SecPerClus;
			this->ReadBuf(szBuf, offset, clusterSize);

			//�������е�ÿ���ļ���
			for (UINT32 i = 0; i < clusterSize ; i += 32)
			{
				DIR_ENTRY_s *dirEntry = (DIR_ENTRY_s*)(szBuf + i);
				if (dirEntry->name[0] == 0)
				{
					break;
				}
				if (dirEntry->size == 0 || dirEntry->size == 0xFFFFFFFF)
				{
					//������ļ��У�����Ҫ�����ļ������Ƿ���ɾ�����ļ�
					UINT32	tmpClusterNum = this->ParseStartCluster(dirEntry);
					if (tmpClusterNum != 0 && tmpClusterNum < m_fatNum && m_clusterFlag[tmpClusterNum] == 0)
					{
						dirs.push(tmpClusterNum);
						m_clusterFlag[tmpClusterNum] = 1;//�����ظ���ͬһ�ؽ��з���
					}
				}

				//��¼��ǰ�ļ���
				DIR_ENTRY_s *firstEntry = dirEntry;
				//�ҵ���һ���ǳ��ļ�����־���ļ���
				while (dirEntry->attr == 0x0F)
				{
					i += 32;
					dirEntry = (DIR_ENTRY_s *)(szBuf + i);
				}
				//��ɾ�����ļ�
				if (dirEntry->name[0] == 0xE5 && dirEntry->size != 0 && dirEntry->size != 0xFFFFFFFF)
				{
					if (firstEntry == dirEntry)
					{
						this->ParseShortFileName(dirEntry, fileName, MAX_PATH);
					}
					else
					{
						this->ParseLongFileName(fileName, MAX_PATH, firstEntry, dirEntry);
					}

					CBaseFileObject	*fileObject = new CBaseFileObject;
					File_Content_Extent_s	*fileExtent = NULL;
					this->GetFileExtent(dirEntry, &fileExtent);
					fileObject->SetFileName(fileName);
					fileObject->SetFileSize(this->ParseFileSize(dirEntry));
					fileObject->SetAccessTime(this->ParseAccessDate(dirEntry));
					fileObject->SetCreateTime(this->ParseCreateDate(dirEntry));
					fileObject->SetModifyTime(this->ParseModifyDate(dirEntry));
					fileObject->SetFileExtent(fileExtent);
					fileArray.push_back(fileObject);
				}
			}
			clusNum = this->GetNextCluster(clusNum);
			if (clusNum == 0)
			{
				break;
			}
		}
	}
	free(szBuf);
}

UINT32	CFat32FileSystem::GetNextCluster(UINT32 prmCurCluster)
{
	if (prmCurCluster >= m_fatNum)
	{
		return 0;
	}
	if (m_fatTable[prmCurCluster] == EOC || m_fatTable[prmCurCluster] == 0x0FFFFFF8)
	{
		return 0;
	}
	return m_fatTable[prmCurCluster];
}

void CFat32FileSystem::ParseShortFileName(DIR_ENTRY_s *prmDirEntry, TCHAR prmFileName[], size_t prmFileNameLength)
{
	char	tmpFileName[20] = {0};
	memset(prmFileName, 0, prmFileNameLength);
	int i = 0;
	for (i = 0; i < 8; i++)
	{
		if (prmDirEntry->name[i] == ' ')
		{
			break;
		}
		tmpFileName[i] = prmDirEntry->name[i];
	}
	if (prmDirEntry->name[8] != ' ')
	{
		tmpFileName[i++] = '.';
	}
	for (int j = 8; j < 11; j++)
	{
		if (prmDirEntry->name[j] == ' ')
		{
			break;
		}
		tmpFileName[i++] = prmDirEntry->name[j];
	}
#ifdef _UNICODE
	::MultiByteToWideChar(CP_THREAD_ACP,0,tmpFileName,-1,prmFileName,prmFileNameLength);
#else
	memcpy(prmFileName,tmpFileName,prmFileNameLength);
#endif
}

void CFat32FileSystem::ParseLongFileName(TCHAR prmLongFileName[], size_t prmFileNameLength, DIR_ENTRY_s *prmFirstEntry, DIR_ENTRY_s *prmLastEntry)
{
	TCHAR	tmpFileName[1024] = { 0 };
	memset(prmLongFileName, 0, prmFileNameLength);
	UINT32	index = 0;
	//���ļ����Ļ�ȡ��ʽΪ���������ҵ�˳������
	DIR_LONG_ENTRY_s *startEntry = (DIR_LONG_ENTRY_s*)(prmFirstEntry);
	DIR_LONG_ENTRY_s *dirLongEntry = (DIR_LONG_ENTRY_s*)(prmLastEntry);
	dirLongEntry--;
	while (startEntry != dirLongEntry)
	{
		memcpy((char*)tmpFileName + index, dirLongEntry->name0_4, 10);
		index += 10;
		memcpy((char*)tmpFileName + index, dirLongEntry->name5_10, 12);
		index += 12;
		memcpy((char*)tmpFileName + index, dirLongEntry->name11_12, 4);
		index += 4;
		dirLongEntry--;
	}
	memcpy((char*)tmpFileName + index, startEntry->name0_4, 10);
	index += 10;
	memcpy((char*)tmpFileName + index, startEntry->name5_10, 12);
	index += 12;
	memcpy((char*)tmpFileName + index, startEntry->name11_12, 4);
	index += 4;
#ifndef _UNICODE
	DWORD tmpLength = ::WideCharToMultiByte(CP_THREAD_ACP, 0, (WCHAR*)tmpFileName, -1, NULL, 0, 0, 0);
	if (tmpLength > prmFileNameLength)
	{
		return;
	}
	::WideCharToMultiByte(CP_THREAD_ACP, 0, (WCHAR*)tmpFileName, -1, prmLongFileName, prmFileNameLength, 0, 0);
#else
	memcpy(prmLongFileName,tmpFileName,_tcslen(tmpFileName)*sizeof(TCHAR));
#endif
}

UINT32	CFat32FileSystem::ParseStartCluster(DIR_ENTRY_s *prmDirEntry)
{
	UINT32 result = (prmDirEntry->starthi << 16)&(0xFFFF0000);
	result = result | (prmDirEntry->start & 0xFFFF);
	if (result == EOC || result == 0x0FFFFFF8)
	{
		return 0;
	}
	return result;
}

CBaseFileObject *CFat32FileSystem::ParseFileObject(DIR_ENTRY_s *prmFirstEntry,DIR_ENTRY_s *prmLastEntry)
{
	TCHAR		tmpFileName[1024] = {0};
	CBaseFileObject		*tmpFileObject = new CBaseFileObject;
	UINT32		tmpSectors = m_fatSector.BPB_FATSz32*m_fatSector.BPB_NumFATs;
	tmpSectors += m_fatSector.BPB_ResvdSecCnt;
	if(tmpFileObject==NULL)
	{
		return tmpFileObject;
	}
	if(prmFirstEntry == prmLastEntry)
	{
		this->ParseShortFileName(prmFirstEntry,tmpFileName,sizeof(tmpFileName));
	}
	else
	{
		this->ParseLongFileName(tmpFileName,sizeof(tmpFileName),prmFirstEntry,prmLastEntry);
	}
	tmpFileObject->SetFileName(tmpFileName);
	//���������ļ���ʼ�Ĵغţ�ת��Ϊ����ڷ���ƫ�Ƶ����������б���
	UINT32		tmpClusterNum = this->ParseStartCluster(prmLastEntry);
	tmpSectors += (tmpClusterNum-m_fatSector.BPB_RootClus)*m_fatSector.BPB_SecPerClus;
	tmpFileObject->SetFileStartSector(tmpSectors);
	if(prmLastEntry->attr & 0x10)
	{
		//Ŀ¼����
		tmpFileObject->SetFileSize(0);
		tmpFileObject->SetFileType(FILE_OBJECT_TYPE_DIRECTORY);
	}
	else
	{
		tmpFileObject->SetFileSize(prmLastEntry->size);
		tmpFileObject->SetFileType(FILE_OBJECT_TYPE_FILE);
	}
	return tmpFileObject;
}

UINT32 CFat32FileSystem::GetFileExtent(DIR_ENTRY_s *dirEntry, File_Content_Extent_s **prmExtent)
{
	File_Content_Extent_s *p = *prmExtent;
	UINT32	baseSector = m_fatSector.BPB_NumFATs*m_fatSector.BPB_FATSz32 + m_fatSector.BPB_ResvdSecCnt;
	UINT32	startCluster = this->ParseStartCluster(dirEntry);
	UINT32	clusterSize = m_fatSector.BPB_SecPerClus;
	if (startCluster == 0)
	{
		return 0;
	}
	if (p == NULL)
	{
		p = new File_Content_Extent_s;
		*prmExtent = p;
	}
	p->startSector = startCluster*m_fatSector.BPB_SecPerClus + baseSector - m_fatSector.BPB_RootClus*m_fatSector.BPB_SecPerClus;
	UINT32 fileSize = dirEntry->size;
	fileSize += 511;
	fileSize = fileSize & ~(511);
	p->totalSector = fileSize >> 9;

	return 1;
}

CStringUtil CFat32FileSystem::ParseCreateDate(DIR_ENTRY_s *dirEntry)
{
	char	szBuf[128] = { 0 };
	USHORT	time = dirEntry->ctime;
	USHORT	date = dirEntry->cdate;
	UINT8   hour = (time & 0xF800) >> 11;
	UINT8   minute = (time & 0x7E0) >> 5;
	UINT8	second = (time & 0x1F);
	UINT8	year = (date & 0xFE00) >> 9;
	UINT8   month = (date & 0x1E0) >> 5;
	UINT8	day = (date & 0x1F);
	sprintf_s(szBuf, 128, _T("%04d-%02d-%02d %02d:%02d:%02d"), year + 1980, month, day, hour, minute, second);
	return szBuf;
	//fileInfo->createDate = szBuf;
}

CStringUtil CFat32FileSystem::ParseModifyDate(DIR_ENTRY_s *dirEntry)
{
	char	szBuf[128] = { 0 };
	USHORT	time = dirEntry->time;
	USHORT	date = dirEntry->date;
	UINT8   hour = (time & 0xF800) >> 11;
	UINT8   minute = (time & 0x7E0) >> 5;
	UINT8	second = (time & 0x1F);
	UINT8	year = (date & 0xFE00) >> 9;
	UINT8   month = (date & 0x1E0) >> 5;
	UINT8	day = (date & 0x1F);
	sprintf_s(szBuf, 128, _T("%04d-%02d-%02d %02d:%02d:%02d"), year + 1980, month, day, hour, minute, second);
	return szBuf;
	//fileInfo->modifyDate = szBuf;
}

CStringUtil CFat32FileSystem::ParseAccessDate(DIR_ENTRY_s *dirEntry)
{
	char	szBuf[128] = { 0 };
	USHORT	date = dirEntry->adate;
	UINT8	year = (date & 0xFE00) >> 9;
	UINT8   month = (date & 0x1E0) >> 5;
	UINT8	day = (date & 0x1F);
	sprintf_s(szBuf, 128, _T("%04d-%02d-%02d"), year + 1980, month, day);
	return szBuf;
	//fileInfo->modifyDate = szBuf;
}