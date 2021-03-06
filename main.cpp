#include "CppUtilClasses/Matrix.h"
#include "CppUtilClasses/StringUtil.h"
#include <iostream>
#include <string>
#include <fstream>
#include <limits>
#include <cctype>
#include <map>
#include "bedSeq/RandomAccessFile.h"
#include "CppBioClasses/Nucleic.h"



#define FORWARD_STRAND '+'
#define REVERSE_STRAND '-'
#define BOTH_STRANDS '.'

#define SEQFILE_SUFFIX ".seq"
#define FOLDER_SEPARATOR "/"

#define PWMFINDER_BUFFER_SIZE 1024

using namespace std;

class PosMatrix
{
	public:
	Matrix<double> matData;
	void readFile(const string&filename){
			
	}
};


class KmerScorer{
	public:
	virtual int score(const char* _kmer,int abortScore) const{return 0;}
	virtual int getLength() const{return 0;}
	virtual string toString() const{return "";}
};

class SimpleConsensusKmerScorer:public KmerScorer{
	private:
	int mismatch;
	string consensus;
	public:
	SimpleConsensusKmerScorer(const string& _consensus, int _mismatch):consensus(_consensus),mismatch(_mismatch){}
	string toString() const{
		return consensus+"/"+StringUtil::str(mismatch);	
	}
	int score(const char*_kmer,int abortScore) const{
		const char*consensus_c_str=consensus.c_str();
		int consensus_length=consensus.length();
		int mismatches=0;
		for(int i=0;i<consensus_length;i++){
			if (toupper(consensus[i])!=toupper(_kmer[i]))
			{
				mismatches++;
				if (mismatches>mismatch){
					return  numeric_limits<int>::min();
				}
			}
		}
		
		return int(float(consensus_length-mismatches)/consensus_length*10000);
	}
	
	int getLength() const
	{
		return consensus.length();
	}
	
	SimpleConsensusKmerScorer* createReverseComplementScorer() const;
	
};


SimpleConsensusKmerScorer* SimpleConsensusKmerScorer::createReverseComplementScorer() const
{
	SimpleConsensusKmerScorer* newScorer=new SimpleConsensusKmerScorer(reverse_complement(this->consensus),this->mismatch);	
	return newScorer;
}


class SimplePWMKmerScorer:public KmerScorer
{
	
	
	public:
	int minScore;
	Matrix<int> matData;
	int consensusBase;
	
	SimplePWMKmerScorer(const string& _filename, int _minScore):minScore(_minScore)
	{
		if(_filename.length()>0)
		{	

			StringToInt s2i;
			consensusBase=0;
			//cerr<<"Reading file "<<_filename<<endl;
			matData.readFile(_filename,s2i); //else init empty.
			//cerr<<"Done reading"<<endl;
			//matData.print(cerr);
			for(int i=0;i<matData.getNumberOfRows();i++){
				int maxI=numeric_limits<int>::min();
				for(int j=0;j<4;j++){
					int thisItem=matData.itemAt(i,j);
					if(thisItem>maxI){
						maxI=thisItem;	
					}
				}
				
				consensusBase+=maxI;
			}
			
		}
		
	}
	string toString() const{
		return "PWM";	
	}
	int score(const char*_kmer,int abortScore) const{
		
		int consensus_length=this->getLength();
		//int gross_score=0;
		int currentScore=-this->consensusBase;
		//A:0 C:1 G:2 T:3
		
		for(int i=0;i<consensus_length;i++){
			switch (toupper(_kmer[i]))
			{
				case 'A':
					currentScore+=matData.itemAt(i,0);
					break;
				case 'C':
					currentScore+=matData.itemAt(i,1);
					break;
				case 'G':
					currentScore+=matData.itemAt(i,2);
					break;
				case 'T':
					currentScore+=matData.itemAt(i,3);
					break;
				default:
					//not ACGT, return this as invalid
					return numeric_limits<int>::min();	
			}
	
			
			if (currentScore<minScore || currentScore<abortScore){ //this sequence or sequence with this prefix already has a lower score than the min score or the lowest score on record of the topN
				return numeric_limits<int>::min(); //abort
			}	
		}
		
		return currentScore;
		
		
	}
	
	int getLength() const
	{
		return matData.getNumberOfRows();
	}
	
	SimplePWMKmerScorer* createReverseComplementScorer() const;
	
};


SimplePWMKmerScorer* SimplePWMKmerScorer::createReverseComplementScorer() const
{	
	//cerr<<"copy"<<endl;
	SimplePWMKmerScorer* newScorer=new SimplePWMKmerScorer(*this); //create a new matrix
	//cerr<<"copied "<<newScorer->matData.getNumberOfRows()<<"x"<<newScorer->matData.getNumberOfCols()<<endl;
	
	//rPWM[i,j]=[k-i,4-j]
	int k=this->getLength();
	for(int i=0;i<k;i++){
		for(int j=0;j<4;j++){
			//cerr<<"set "<<i<<" "<<j<<endl;
			newScorer->matData.itemAt(i,j)=this->matData.itemAt(k-i-1,4-j-1);	
		}	
	}
	
	//cerr<<"new done"<<endl;
	
	return newScorer;
		
}



class PWMHitsFinder
{
	
	public:
	
	class Hit
	{
		public:
		int score;
		string ref;
		int start0;
		int end1;
		char strand;
		string prefix;
		Hit(int _score,const string& _ref,int _start0,int _end1,char _strand,string _prefix):score(_score),ref(_ref),start0(_start0),end1(_end1),strand(_strand),prefix(_prefix){}
		void print(ostream& os) const{
			string name=ref+":"+StringUtil::str(start0+1)+"-"+StringUtil::str(end1)+":"+strand;
			os<<prefix<<ref<<"\t"<<start0<<"\t"<<end1<<"\t"<<name<<"\t"<<score<<"\t"<<strand<<endl;
		}
	};
	
	int topN;
	KmerScorer* scorer;
	KmerScorer* reverseScorer;
	int bufferSize;
	multimap<int,Hit,greater<int> > hits;
	int abortScore;
	
	PWMHitsFinder(KmerScorer* _scorer,KmerScorer* _reverseScorer,int _topN,int _bufferSize):scorer(_scorer),reverseScorer(_reverseScorer),topN(_topN),bufferSize(_bufferSize)

	{
		
		abortScore=numeric_limits<int>::min(); // no abortion at the very begining
	}
	
	void printHits(ostream& os){
		for(multimap<int,Hit,greater<int> >::iterator i=hits.begin();i!=hits.end();i++){
			(*i).second.print(os);
		}
	}
	
	
	void proposeHit(const Hit& hit)
	{
		if (hit.score==numeric_limits<int>::min())
			return;
			
		hits.insert(map<int,Hit,greater<int> >::value_type(hit.score,hit));
		if(topN>0){
			if(hits.size()>topN){
				//cerr<<"try erase"<<endl;
				hits.erase((++hits.rbegin()).base()); //remove the (topN+1)-ranked hit 			
				//cerr<<"erase ok"<<endl;	
			}
			
			this->abortScore=hits.rbegin()->second.score; //save the lowest score in the rank so that we can save time on scoring if scoring drops off.
			
		}
		
	
	}
	
	void scanSeq(RandomAccessFile & raf,const string& ref,int gstart0,int gend1,char strand,string prefix="")
	
	
	{
		
		
		if(!scorer){
			if(strand==FORWARD_STRAND || strand==BOTH_STRANDS){
				cerr<<"forward scorer not specified"<<endl;
				return;
			}
		}	
		
		if(!reverseScorer){
			if(strand==REVERSE_STRAND || strand==BOTH_STRANDS){
				cerr<<"reverse scorer not specified"<<endl;
				return;	
			}	
		}
		
		
		
		string buffer;
		int curgstart0=gstart0;
		
		
		buffer=raf.get(curgstart0,min(curgstart0+bufferSize,gend1));

		const char*buffer_c_str=buffer.c_str();
		int k=scorer->getLength();
		int fedSize=buffer.length();
		const char*curKmer;


		
		while(fedSize>=k){

			for(int i=0;i<fedSize-k+1;i++){

				
				int absg0=curgstart0+i;
				curKmer=&buffer_c_str[i];
			

				
				if (strand==FORWARD_STRAND || strand==BOTH_STRANDS){
					//score forward strand here
					
					int score=scorer->score(curKmer,this->abortScore);
					Hit thisHit(score,ref,absg0,absg0+k,FORWARD_STRAND,prefix);
					this->proposeHit(thisHit);
				}	
				if (strand==REVERSE_STRAND || strand==BOTH_STRANDS){
					//score reverse strand here
					
					int score=reverseScorer->score(curKmer,this->abortScore);
					
					/*if (absg0==1919525){
						cerr<<"reverseScorer:"<<reverseScorer->toString()<<endl;
						cerr<<buffer.substr(i,17)<<" score at 1919526="<<score<<endl;	
					}*/
					
					Hit thisHit(score,ref,absg0,absg0+k,REVERSE_STRAND,prefix);
					this->proposeHit(thisHit);
				}	
				//cerr<<"done "<<i<<endl;
			}
			
			
			//advance curgstart0
			curgstart0+=fedSize-k+1;
			
			//now advance buffer window
			buffer=raf.get(curgstart0,min(curgstart0+bufferSize,gend1));
			buffer_c_str=buffer.c_str();
			fedSize=buffer.length();

		}
		
		
			
	}
	
	
	
	
};


int printUsage(const char*programName){
	cerr<<"Usage: "<<programName<<" seqFolder searchRangeDef mode motifDef modeParam topN"<<endl;
	cerr<<"mode=consensus motifDef=consensusString modeParam=mismatch"<<endl;
	cerr<<"mode=PWM motifDef=PWMRowMatFile modeParam=PWMMinScore"<<endl;
	
	return 1;
}

int main(int argc,char** argv){
	/*Matrix<double> M;
	vector<double> row;
	row.push_back(0.0);
	row.push_back(0.5);
	row.push_back(0.7);
	row.push_back(0.8);
	
	for(int i=0;i<3;i++){
		for(int j=0;j<4;j++){
			row[j]+=1;	
		}	
		
		M.addRow(row);
	}
	
	M.print(cout);
	cerr<<"change 0,0"<<endl;
	M.itemAt(0,0)=-20;
	M.print(cout);
	M.finalize();
	cout<<"finalize"<<endl;
	M.print(cout);
	M.itemAt(1,2)=-10;
	cout<<"change 1,2"<<endl;
	M.print(cout);
	cout<<"read matrix"<<endl;
	Matrix<double> Mf;
	StringToFloat stf;
	Mf.readFile("matrix.dat",stf);
	Mf.print(cout);
	Mf.finalize();
	cout<<"finalize read matrix"<<endl;
	Mf.print(cout);*/
	
	/*RandomAccessFile hello("helloworld.txt");
	cout<<"1a:"<<hello.get(2,6)<<endl;
	cout<<"1b:"<<hello.get(4,9)<<endl;
	cout<<"1c:"<<hello.get(2,6)<<endl;
	cout<<"2:"<<hello.get(5,23)<<endl;
	//cout<<hello.get(100,12)<<endl;
	cout<<"3:"<<hello.get(2,6)<<endl;
	
	return 0;*/
	
	const char* programName=argv[0];
	
	if(argc<7){
		return printUsage(programName);
	}
	
	string seqFolder=argv[1];
	string searchRangeDef=argv[2];
	string mode=argv[3];
	string motifDef=argv[4];
	int modeParam=StringUtil::atoi(argv[5]);
	int topN=StringUtil::atoi(argv[6]);
	
	KmerScorer* scorer=NULL;
	KmerScorer* rscorer=NULL;
	
	if(mode=="consensus"){
		SimpleConsensusKmerScorer* scks=new SimpleConsensusKmerScorer(motifDef,modeParam);
		scorer=scks;
		rscorer=scks->createReverseComplementScorer();	
	}else if(mode=="PWM")
	{	
		if (modeParam==0){
			modeParam=numeric_limits<int>::min();
		}
		
		cerr<<"reading matrix from "<<motifDef<<endl;
		SimplePWMKmerScorer*spks=new SimplePWMKmerScorer(motifDef,modeParam);
		cerr<<"Done reading"<<endl;
		spks->matData.print(cerr);
		cerr<<"consensus base:"<<spks->consensusBase<<endl;
		scorer=spks;
		
		cerr<<"create reverse complement matrix"<<endl;
		SimplePWMKmerScorer*rspks=spks->createReverseComplementScorer();
		rspks->matData.print(cerr);
		rscorer=rspks;
		
		//cerr<<"done"<<endl;
	}
	else{
		cerr<<"undefined mode "<<mode<<endl;
		return printUsage(programName);	
	}
	
	PWMHitsFinder Finder(scorer,rscorer,topN,PWMFINDER_BUFFER_SIZE);
	
	vector<string> splits;
	vector<string> splits2;
	
	//now start scanning
	fstream rangeIn(searchRangeDef.c_str());
	string rangeString;
	string ref;
	int start1;
	int end1;
	char strand;
	string curRef;
	
	RandomAccessFile *curRaf=NULL;
	
	while(rangeIn.good()){
		rangeString="";
		rangeIn>>rangeString;
		if(rangeString.length()>1){
			cerr<<"processing "<<rangeString<<endl;
			start1=1;
			end1=numeric_limits<int>::max();
			strand=BOTH_STRANDS;
			
			StringUtil::split(rangeString,":",splits);
			
			//cerr<<splits.size()<<endl;
			ref=splits[0];
			//cerr<<ref<<endl;
			if(ref!=curRef){
				if(curRaf){
					curRaf->close();
					delete curRaf;

				}
				
				string filename=seqFolder+FOLDER_SEPARATOR+ref+SEQFILE_SUFFIX;
				cerr<<"open file "<<filename<<endl;
				curRaf=new RandomAccessFile(filename);
				
				curRef=ref;	
			}
			
			if(splits.size()>=2){
				string coordinateString=splits[1];
				StringUtil::split(coordinateString,"-",splits2);
				if(splits2.size()<2){
					cerr<<"coordinate string needs start1-end1 or -end1, or start1-"<<endl;
					continue;	
				}
				
				if(splits2[0].length()>0){
					start1=StringUtil::atoi(splits2[0]);
				}
				
				if(splits2[1].length()>0){
					end1=StringUtil::atoi(splits2[1]);	
				}
			}
			//char strand;
			
			if(splits.size()>=3){
				strand=splits[2][0];
				switch(strand){
					case FORWARD_STRAND:
					case REVERSE_STRAND:
					case BOTH_STRANDS:
					break;
					default:
						cerr<<"stand must be + , - or ."<<endl;
						continue;	
				}	
			}
			
			Finder.scanSeq(*curRaf,ref,start1-1,end1,strand,rangeString+"\t");
			//Finder.printHits(cout,rangeString+"\t");
			
				
		}
	}
	rangeIn.close();
	Finder.printHits(cout);
	
	if(curRaf){
		curRaf->close();
		delete curRaf;	
	}
	
	delete scorer;
	
	return 0;
	
}