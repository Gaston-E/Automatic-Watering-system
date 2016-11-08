
/**********************************************************
 * Programme général avec communication externe Ethernet  *
 *              Arroseur-Carte-MEGA_V7.ino                *
 *       pour tester la boucle de prog avec le web        *
 *       Ajout de la mesure de température et d'humidité  *
 *       de l'appartement                                 *
 *       Le programme fonctionne correctement y           *
 *       compris tous les affichages en javascript        *
 *********************************************************              
 
  */
 /* ATTENTION : 
  *  Ce programme n'est PAS UTILISABLE sur carte UNO 
  *  mais sur carte MEGA avec carte SD du shield Ethernet:
  *  La lecture des paramètres de fonctionnement se fait sur la carte SD (fichier params.txt) 
  *  les donnees d'arrosage sont mémorisées dans les fichiers datas.txt et historic.txt
  *  l'horodatage se fait avec la carte RTC DS3231
  *  
  *  Avant de charger le programme en mémoire il faut lancer le programme
  *  d'initialisation de l'EEPROM (Init_Arroseur.ino): => mise en mémoire des données de fonctionnement et
  *  Drapeau d'arrosage effectué (arrosage_Fait = 1 dans ce cas et 0 à l'initialisation)
  *  afin d'éviter d'arroser deux fois de suite en cas de coupure de courant et mettre
  *  à 0 le drapeau qui signale les fuites
  *  La partie monitoring web fonctionne correctement et est incluse dans ce programme
  *  affichage de l'humidité et température de l'appartement compris
  */
  
#include <EEPROM.h>
#include <Stepper.h>
#include <DS3231.h>
#include <DS3232RTC.h> 
#include <Time.h>
//#include <TimeLib.h>
#include <Wire.h>
#include <Ethernet.h> // librairie Ethernet
#include <WiFi.h> // pour la carte SD
#include <SPI.h>
#include <SD.h>
#include "DHT.h"

DS3231  rtc(SDA, SCL); // sur carte MEGA : pin 20 = SDA ; pin21 = SCL
  /* voir jumpers sur carte horloge + connections sur MEGA  */
 
    /********* lignes pour la mesure d'humidité  *****/
#define A 11 // A de selection pot
#define B 12
#define C 13
#define D 9
#define alimDetect 8 //borne de commande du relais d'alimentation des détecteurs

volatile int fuites = LOW; // variable de type volatile (en SRAM pour interrupt)
# define led_Alarme A2 // led alarme rouge carte MEGA ou UNO

    // ligne d'entrée digitale pour la position de référence opto
# define pos_pot0 3  // ligne d'interruption donc logique et disponible avec UNO
int pos_O = digitalRead(pos_pot0);

/************ Moteur PAP *************** */
    // lignes de commande moteur: sorties 6 et 7
# define phase1 6
# define phase2 7
     // paramètres de fonctionnement: moteur 48 pas par tour
#define nb_pas 48
int vitesse = 30; //  N moteur = 1/2 t par seconde (30 t/min)

Stepper moteurPAP(nb_pas, phase1, phase2);

     /*** ligne d'alimentation de la pompe  ***/
#define pinPompe 5

/* ******** Variables globales à modifier suivant pot et plante ********************
    - Distributeur pour 12 pots -  nombre de pots - 1 ( 12 pots n°0 à 11 )
     la matrice dose_pot [] contient la durée de fonctionnement de la pompe pour chaque pot */
     
int nbrePots; // nombre de pot = 12 de 0 à 11
     /* la matrice val_Limite[] contient la valeur limite de la valeur de mesure 'd'humidité' pour chaque pot */ 
int val_Limite[12]; // = {250, 450, 350, 280, 250, 380, 350, 290, 400, 300, 260, 500}; pour debug
//int dose_pot[12] ; //= {1, 2, 12, 4, 11, 6, 13, 2, 11, 5, 8, 12}; // temps d'arrosage en secondes pour test
String plantes[12]; //={"F. Lyrata 1","F. Lyrata 2","bananier 1","Bananier 2","Dracena 1","Dracena 2","Beaucarnea","Rose Caraibe","Mandarinier","Olivier","Laurier","Ficus"};

String ligne ="";
char nomFichier[]="datas.txt";
char nomFichier1[]="params.txt"; // lecture sur carte SD
char nomFichier2[] = "plantes.txt"; // lecture sur carte SD
String strLine=""; // pour la fonction getLine()

String params[12];
int resist[12]; // valeurs limites de sècheresse (0 - 1023)
int duree[12]; // durée d'arrosage en secondes

float Vmin = 44; // Rmin = 1 K-ohm mini -> Nmin = 44 valeur pour pot saturé en eau
// float R1 = 22; // Résistance sur connexions capteurs = 22 K-ohms
int val = 0; // etat du pot sur l'entrée analogique 0 ou 1
int codePots = 0; // état des différents pots, chaque bit représente l'état d'un pot
int lu_pots = 0; // Flag pour lire l'état des pots à 18h seulement
int lu_6h = 0; // Flag pour lire l'état des pots à 6h seulement
byte adress_lu_6h = 40; // pour mémorisation en EEPROM voir init_Arrosage.ino
byte adress_lu_pots = 41; // "

//int etatPot = 0; 
int analogPin0= 0; // entrée 0 pour pots de 8 à 11
int analogPin1= 1; // entrée analogique 1 pour pots de 0 à 7
int i = 0; // n° de pot

    // mesures[nbrePots - 1]; // pour mémoriser les mesures faites
int mesures[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // on efface les mesures precedentes
int taux_Hum[12] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}  ;
int adress_arrosage =0;

  /***** pour alarme *****/
byte arrosage = 1; // à priori on est là pour arroser, 0 si fuites
   /* pour eviter les pb de redémarrage - ré-arrosage - après coupure de courant */
int adress_arrosage_Fait =1; // pour accès à l'EEPROM
byte arrosage_Fait = 0; // variable associee à l'état de l'arrosage 0 si non fait, 1 si oui
 
  /* pour horloge RTC */ 
String date_jour;  // pour carte SD
String heuredu_Jour; //  "
String jour;
int heure= 0;
String Str_Num="";

  /* pour l'utilisation de la carte SD */
const int chipSelect = 53;  //pin 10 de l'Arduino reliee au CS du module SD
const int selectSD=4;
Sd2Card card;
SdVolume volume;
SdFile root;
File myFile;
char car; //pour la fonction getLine
int index=0;
int date_Aros[4]={0,0,0,0}; // pour la date du dernier arrosage

  /* Pour le module Ethernet */

 String temp="";
    //--- l'adresse mac = identifiant unique du shield
    // à fixer arbitrairement ou en utilisant l'adresse imprimée sur l'étiquette du shield
byte mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0x1A, 0x71 };
    //----- l'adresse IP fixe à utiliser pour le shield Ethernet ---
IPAddress ipLocal(192,168,0,100); // l'adresse IP locale du shield Ethernet hors zone DNS
/*  sur la freebox la connexion se fait par : http://82.232.193.106:5003/ */
 
  //--- création de l'objet serveur ----
EthernetServer serveurHTTP(5003); // crée un objet serveur utilisant le port 5003 = port HTTP
String chaineRecue=""; // déclare un string vide global pour réception chaine requete
int comptChar=0; // variable de comptage des caractères reçus

    /* pour capteur température et humidité appartement */

DHT  dht;
  float humidity;
  float temperat; 
int temp_Appart =25;
int hum_Appart=48;
int temp_hum[2]; //tableau 2 valeurs

                        /*****     setup     *****/
                        
void setup() 
  {
  Serial.begin(115200);
    // Initialize the rtc object
  rtc.begin();
delay(1000);
  pinMode(analogPin0, INPUT); // AN0 en entrée
  pinMode(analogPin1, INPUT); //AN1 en entrée
  pinMode(alimDetect, OUTPUT);
  pinMode(led_Alarme, OUTPUT);

  analogReference(DEFAULT); // référence de tension = 5V
  pinMode(A, OUTPUT); // pins 9 à 13 en sorties
  pinMode(B,OUTPUT);
  pinMode(C,OUTPUT);
  pinMode(D,OUTPUT);

  pinMode(phase1, OUTPUT);
  pinMode(phase2, OUTPUT);
  moteurPAP.setSpeed(vitesse);
 
    /*** Interruptions matérielles sur Int0 - pin D2 ***/
  attachInterrupt(0,Alarm_Function,FALLING);
  pinMode(led_Alarme, OUTPUT); // led connectée sur pin A2
  pinMode(pos_pot0, INPUT); // position initiale distributeur sur digital pin 3

/* pour shield Ethernet + SD */ 
//  pinMode(10, OUTPUT); // pour la carte UNO
  pinMode(53, OUTPUT); // pour la carte Arduino MEGA. Peu importe la valeur de chipSelect.
  pinMode(4, OUTPUT); // pour la carte SD

/*   pour utiliser l'heure   */
   setSyncProvider(RTC.get);   // the function to get the time from the RTC

/* Initialisation carte SD  */
    Serial.println("Initialisation de la carte SD en cours...");
    if (!SD.begin(chipSelect)) { // si initialisation avec broche selectSD en tant que CS n'est pas réussie
      Serial.println("Echec initialisation!"); // message port Série
      return; // sort de setup()
      } // if SD begin()
      
/* ----- Lecture des paramètres de fonctionnement ----- */
/* ----- Nbre de pots, Resistance limite, dose de chaque pot -----  */
    //--- si initialisation réussie : on se place ici :
  Serial.println("Initialisation reussie !"); // message port Série
  Serial.println ("Contenu du fichier params.txt :");
  
  Serial.print("Nombre de pots = ");
  ligne=getLine(nomFichier1,2, true); // ligne 2 du fichier
  nbrePots=ligne.toInt();
  Serial.println(nbrePots); // OK
  
     /* ------ lecture 'resistance' limite (valeur de mesure limite): OK  ----------- */
  int debut=4; // ligne 4 du fichier params.txt
  int fin=15;  // ligne 15 du fichier
   for (int j=debut ; j<= fin ; j++) 
    {
    ligne=getLine(nomFichier1,j, false); // fonction lecture de ligne avec messages de debug
    params[j-debut]=ligne;
    resist[j-debut]=ligne.toInt();
    val_Limite[j-debut]=resist[j-debut];
    Serial.print("j = ");
    Serial.print(j);
    Serial.print(" --> ");
    Serial.print("R = ");
    Serial.println(params[j-debut]);
    } // fin lecture Resistance limite
    Serial.println("-----------------------");

    /* ------lecture durée d'arrosage : OK ----------- */
    debut=18; // ligne 18 du fichier params.txt
    fin=29;
    for (int j=debut ; j<=fin; j++) 
    {
    ligne=getLine(nomFichier1,j, false); // fonction lecture de ligne avec messages de debug
    duree[j-debut]=ligne.toInt();
    Serial.print("j = ");
    Serial.print(j);
    Serial.print(" -> ");
    Serial.println(duree[j-debut]);
    //ligne="";    
    }// fin lecture duree d'arrosage
    Serial.println("-----------------------");

    /* ----- lecture liste des plantes : OK ----- */

 for (int j=1; j<=12; j++) 
    {
    ligne=getLine(nomFichier2,j, false); // fonction lecture de ligne avec messages de debug
    plantes[j-1]=ligne;
    Serial.print("Plante n° "); // debug
    Serial.print(j);
    Serial.print(" : ");
    Serial.println(plantes[j-1]);
    }
    Serial.println("-----------------------");
    lu_6h = EEPROM.read(adress_lu_6h); // en cas de Reset
    lu_pots = EEPROM.read(adress_lu_pots); // "
    pos_initFunction();
    ReadEtatPots_Function();
    tauxHumid_Fonction();
    Serial.println("-----------------------");

  /* ---- initialise la connexion Ethernet avec l'adresse MAC du module Ethernet, l'adresse IP Locale ---- */
      //---- +/- l'adresse IP du serveurDNS , l'adresse IP de la passerelle internet et le masque du réseau local
      //Ethernet.begin(mac); // forme pour attribution automatique DHCP - utilise plus de mémoire Flash (env + 6Ko)
Ethernet.begin(mac, ipLocal); // forme conseillée pour fixer IP fixe locale
      //Ethernet.begin(mac, ipLocal, serverDNS, passerelle, masque); // forme complète
delay(1000); // donne le temps à la carte Ethernet de s'initialiser
Serial.print(F("Shield Ethernet OK : L'adresse IP du shield Ethernet est : " ));
Serial.println(Ethernet.localIP());
//---- initialise le serveur ----
serveurHTTP.begin();
delay(2000);
Serial.println(F("Serveur Ethernet Adresse de connexion : http://82.232.193.106:5003"));
Serial.println(F("Serveur Ethernet OK : Ecoute sur le port 5003 (http)" ));

  dht.setup(19); // data pin 19

delay(1000);
temp_hum[0]= 25;
temp_hum[1] = 48; 

Serial.println(" --------- Fin Setup ---------- ");

  } /* --------- Fin Setup ---------- */


void loop()
  {
      if (hour() == 0) 
        {
          lu_pots = 0;
          lu_6h = 0;
          EEPROM.write(adress_lu_pots,0); // initialisations lecture pots, lecture pots à 6h, arrosage fait
          EEPROM.write(adress_lu_6h,0); 
          EEPROM.write(adress_arrosage_Fait, 0); // jour suivant pas encore arrose : arrosage_Fait <-- 0
          heure = 0;
        }
        else
        {
          heure = hour();
        }
Serial.println();
Serial.println("*********************************************");
Serial.print("heure = " );
Serial.print( heure);
Serial.print("  ;  " ); 
val = EEPROM.read(adress_arrosage_Fait); // 0 si pas fait
Serial.print("Variable arrosage fait = " );
Serial.println( val);
Serial.print("          Var. lu_pots =  " );
Serial.print(lu_pots );
Serial.print("  ;  Var. lu_6h = " );
Serial.println(lu_6h);
Serial.println();

delay(1000);
Temp_Hum_Function(); // mesure température et humidité appartement

Serial.print("temp appart =  ");
Serial.println(temp_hum[0]); // affiche température sur le COM x
Serial.print("hum appart =  ");
Serial.println(temp_hum[1]); ; // affiche l'humidité sur le COM x

if (lu_pots == 1) 
    {
      Serial.print(" Var. lu_pots =  " );
      Serial.println(lu_pots );

      // Arrosage pots conditionnel à var arrosage et arrosage non Fait
     if ((arrosage == 1) && (heure >= 18) && (val == 0) )
      {
        arrosage_Function(); // arrosage éventuel à 18h
        Serial.println("arrosage fait");
        // on mémorise en EEPROM pour éviter les problèmes de coupure de courant
        EEPROM.write(adress_arrosage_Fait,1); // arrosage_Fait <-- 1
      }
       
     }
    else
        {
           if ((hour() == 18)&&(lu_pots == 0))
              {
              ReadEtatPots_Function();  // on mesure deux fois par jour, à 6h et 18h
              lu_pots = 1; // pour ne pas lire en continu
              EEPROM.write(adress_lu_pots,1); // en cas de Reset
              Serial.println("lu état pots");
              tauxHumid_Fonction(); // calcul des taux d'humidité des pots
              
              Temp_Hum_Function(); // mesure température et humidité appartement

              Serial.println("-------------------------------");
              lecture_date_Function();
              ecrit_datas_Function(); // sur la carte SD
           }  
        }
        
 if ((hour() == 6)&& (lu_6h == 0))
            {
              Serial.println("On va lire l'état des pots");
              ReadEtatPots_Function();  // on mesure deux fois par jour, à 6h et 18h
              lu_6h = 1;// pour ne pas lire en continu
              EEPROM.write(adress_lu_6h,1); // en cas de Reset
              Serial.println("lu état pots, lu_6h = 1");
              tauxHumid_Fonction(); // calcul des taux d'humidité des pots
              Temp_Hum_Function(); // mesure température et humidité appartement
              Serial.println("-------------------------------");
              lecture_date_Function();
              ecrit_datas_Function(); // sur la carte SD
            }
delay(1000); // pour debug


    /* ---- connection internet ---- */
  EthernetClient client = serveurHTTP.available();
  if (client)
    {
    server_web_Function();
    }

  } // fin boucle loop()
  

            /******  FONCTIONS  ******/

    /******************************************/
void Alarm_Function() 
  {
  digitalWrite(led_Alarme,HIGH); // led rouge ON 
  EEPROM.write(adress_arrosage, 0); // mémorise en EEPROM
  arrosage = 0;
  Serial.println("Led Alarme ON : IL Y A DES FUITES !!!");
  }
/********************************************/

void lecture_date_Function()
    {
     jour = rtc.getDOWStr();
     //Serial.print(" ");
  
  // Send date
    date_jour = rtc.getDateStr();

    Serial.print(jour);
    Serial.print("  ");
    Serial.print(date_jour);
  // Send time
    heuredu_Jour = rtc.getTimeStr();
    Serial.print(" -- ");
    //Serial.print(" : ");
    Serial.println(heuredu_Jour);
    }
    
/********************************************/

void ecrit_datas_Function()
    {
 
    SD.remove("datas.txt"); // on efface le fichier, sinon on ajoute dans le fichier
    Serial.println("fichier datas efface");
    myFile = SD.open("datas.txt", FILE_WRITE); // on le recree
    
    myFile.print(jour); 
    myFile.print("  "); // 2 espaces
    myFile.print(date_jour);
    myFile.print(" - ");
    myFile.println(heuredu_Jour);    
    myFile.println(arrosage); // valeur indiquant si arrosage autorisé ou pas (fuites)
    for (int i=0;i <= nbrePots-1 ;i++) 
      {
        myFile.println(taux_Hum[i]); 
      }
    myFile.close();// fermeture du fichier
    Serial.println("datas enregistrees dans le fichier 'datas.txt' ");
    // ouverture du fichier pour ecriture (il sera cree s'il n'existait pas deja )
    //String text=""; //pour ecriture en fichier
 
  myFile = SD.open("historic.txt", FILE_WRITE);
   if (myFile) 
    {
      Serial.print("Ecriture dans le fichier historique 'historic.txt'...");
        //     myFile.println("date du jour + heure");
      // le texte est ajoute au fichier existant
    myFile.print(jour); 
    myFile.print("  "); // 2 espaces
    myFile.print(date_jour);
    myFile.print(" - ");
    myFile.println(heuredu_Jour);    

      myFile.println(arrosage); // valeur indiquant si arrosage autorisé ou pas (fuites)
        for (int i=0;i <= nbrePots-1 ;i++) 
        {
          myFile.println(taux_Hum[i]); 
        }
    myFile.println("-------------------------------"); // ligne de séparation de données
    myFile.println(""); 
    
    myFile.close();
    Serial.println("c'est fait.");
    Serial.println("-------------------------------");
    }
    } 

        /******** Lecture des capteurs ***********/

void ReadEtatPots_Function()
    {
      for( int j=0 ; j <= nbrePots-1 ; j++) 
        {
          mesures[j] = 0; // on efface les mesures precedentes
        }
    codePots = 0; // efface l'ancien état des pots
    digitalWrite(alimDetect, HIGH); // on alimente les capteurs en énergie
    delay(5000); //on attend 5 secondes avant de lire

    /* le numéro du pot est codé sur les 4 bits A B C D et répercuté sur les sorties
       de sélection du pot.  */
       
    for (int i=0; i<=nbrePots-1;i++)
    {
     if (bitRead(i,0) == 0) {
      digitalWrite(A, LOW);
     }
      else {
      digitalWrite(A, HIGH);
      } 

      if (bitRead(i,1) == 0) {
        digitalWrite(B, LOW);
        }
        else {digitalWrite(B, HIGH);
        } 
      if (bitRead(i,2) == 0) {
        digitalWrite(C, LOW);
        }
      else {digitalWrite(C, HIGH);
      } 
      if (bitRead(i,3) == 0) {
        digitalWrite(D, LOW);
      }
      else {digitalWrite(D, HIGH);
      }
 
  delay(100); // pour débuggage 1 s, sinon 0,1 s
  
       if (i <= 7)
          {
           mesures[i] = analogRead(analogPin1); // lit l'enrée analogique AN1 et l'état du pot n°i
           if (mesures[i] < val_Limite[i])
              {
                bitWrite(codePots,i,0); // byte de poids faible de codePots
                 // valeur faible -> bit N° i = 0 , pas d'arrosage
              }
              else
                {
                  bitWrite(codePots,i,1); // byte de poids faible de codePots
                   // valeur élevée -> bit N° i = 1 , arrosage
                }
                
           }
            else 
            { // pour pots de 8 à 11
             delay(100); // pour laisser passer les transitoires dues au changement d'entrée
             mesures[i] = analogRead(analogPin0); // lit l'enrée analogique AN0
             if (mesures[i] < val_Limite[i])
              {
                bitWrite(codePots,i,0); // byte de poids faible de codePots
                 // valeur faible -> bit N° i = 0 , pas d'arrosage
              }
              else
                {
                  bitWrite(codePots,i,1); // byte de poids faible de codePots
                   // valeur élevée -> bit N° i = 1 , arrosage
                }
             }

       delay (100);
       Serial.print("Pot ");
       Serial.print(i);
       Serial.print(" - ");
       Serial.print(mesures[i]);
       Serial.print(" / ");
       Serial.print(val_Limite[i]);
       if (mesures[i] >= val_Limite[i]) 
          {
         Serial.println("  ->  a arroser");
          }
       else 
          {
          Serial.println(" ->  attendre");
          }
     }
     Serial.print("CodePots = ");
     Serial.println(codePots);
     Serial.print(highByte(codePots), BIN);
     Serial.print(" ");     
     Serial.println(lowByte(codePots), BIN);
     
      digitalWrite(alimDetect, LOW); // on coupe l'alimentation des capteurs
      delay(1000); // pour débuggage
 
    }

      // ******** fonction retour en position initiale du distributeur ************
    void pos_initFunction(){

      while (digitalRead(pos_pot0))
        {
         moteurPAP.step(1); /* un pas après l'autre jusqu'à la position 0,
          l'opto envoi un 0 quand on est en position initiale */
        }
       Serial.println("Distrib. en pos. initiale");
       return ;
    }

      /************ Fonction arrosage *************/
      void arrosage_Function()
      {
       pos_initFunction(); // distributeur en position initiale

           // On arrose le pot qui en a besoin si l'arrosage est autorisé (fuites) 
       for (int i = 0; i <= nbrePots-1; i++) 
       {
        moteurPAP.step(i*(nb_pas/(nbrePots ))); //positionnement distributeur
        if (bitRead(codePots,i) == 1)
            {
            // si la valeur du bit correspondant au n° du pot = 1
            digitalWrite(pinPompe, HIGH); // pompe en marche
            delay(duree[i]*1000); // pour la durée correspondant au pot
            digitalWrite(pinPompe, LOW); // arrêt de la pompe
            delay(2000); // pour écoulement eau et pression nulle dans distributeur
            Serial.print("pot n° ");
            Serial.print(i);
            Serial.println(" arrosé");
            } // fin if
            pos_initFunction(); // retour en position initiale
       } // fin for
          
                               // affichage des variables
  Serial.println("------------------------");
  Serial.println("Arrosage effectue"); 
  Serial.println("------------------------");
 
  /* si installation serveur rajouter la date et l'heure,
    et peut etre les valeurs mesurees
     
   for (i = 0; i <= nbrePots; i++); {
   Serial.println("pot n° " +i + " ; Mesure = " + mesures[i] + " ; val. limite = " + val_Limite[i]);
   }
    
   */

      }
      
/* ----------- Fonction getLine ------------------- */

 String getLine(char* nomFichierIn, int lineNumberIn, boolean debug) { 
  // la fonction reçoit : 
  // > le nom du fichier avec l'extension ( data.txt par ex.)
  // > le numéro de la ligne – 1ère ligne = ligne N°1 
  // > un drapeau pour messages debug
  // la fonction renvoie le String correspondant à la ligne

   File dataFileIn=SD.open(nomFichierIn, FILE_READ); // ouvre le fichier en lecture - NOM FICHIER en 8.3 ++++
 
   if (debug) //Serial.println("------"); 
   
   String strLine=""; 
   int comptLine=0; 
   int lastPosition=0; // dernière position
   int currentPosition=0; // dernière position
//   int fin =0; // marqueur de ligne trouvée et mémorisée
  if (dataFileIn){ // le fichier est True si existe
        // if (debug)Serial.println("Ouverture fichier OK");
      comptLine=0;
      lastPosition=0;
      currentPosition=0;   
      strLine=""; // NECESSAIRE pour les appels successifs de la fonction
      while (dataFileIn.available()) { // tant que des données sont disposnibles dans le fichier
      // le fichier peut etre considéré comme un "buffer" de données comme le buffer du port série
    
        char c = dataFileIn.read(); // lit le caractère suivant
        
        if (c==10) { // si le caractère est \n : c'est une fin de ligne
               comptLine=comptLine+1; 
               
               if (comptLine==lineNumberIn) { // si on a atteint la ligne voulue
                 currentPosition=dataFileIn.position()-1; // mémorise la position actuelle en supprimant le lineFeed
                 
                for (int i=lastPosition; i<currentPosition; i++) { // on défile de la dernière position retenue à la position courante                 
                     dataFileIn.seek(i); // se position en i
                     strLine=strLine+char(dataFileIn.read()); // ajoute le caractère au strLine
                     } // fin for  
                            
                 break; // sort du while 
                                
             } // fin if arrivé à la ligne voulue           
               lastPosition=dataFileIn.position();
 
        } // fin if saut de ligne

        } // fin while available 
      dataFileIn.close(); // fermeture du fichier obligatoire après accès 
        
    } // si fichier existe 
    else { // sinon = si probleme 
      Serial.println("Probleme existance fichier"); 
    } // fin else

    return(strLine); 

 } // fin fonction getLine

    /* ------------ Fonction Calcul Taux d' Humidité --------- */
void tauxHumid_Fonction()
{
    float tH; //taux d'humidité
  for (int i = 0; i <= nbrePots-1 ; i++)
    {
    if (mesures[i] < Vmin){
      mesures[i] = Vmin;
      }
    tH = (float(val_Limite[i])- float(mesures[i]))/(float(val_Limite[i])-Vmin)*100;    
    taux_Hum[i] = int(tH);    
    Serial.print("Pot n° ");
    Serial.print(i);
    Serial.print(" -> Taux_Hum = ");
    Serial.println(taux_Hum[i]);
  } //fin for
}
    
      /* ----- Fonction webserveur ----- */


void server_web_Function()
  {
    // si l'objet client n'est pas vide
    // le test est VRAI si le client existe : message d'accueil dans le Terminal Série
    Serial.println (F("--------------------------"));
    Serial.println (F("Client present !"));
    Serial.println (F("Voici la requete du client:"));

    /************* Réception de la chaine de la requete *************/
      //-- initialisation des variables utilisées pour l'échange serveur/client
    chaineRecue=""; // vide le String de reception
    comptChar=0; // compteur de caractères en réception à 0
    EthernetClient client = serveurHTTP.available();
    if (client.connected()) { // si le client est connecté
    /************** Réception de la chaine par le réseau **************/
      while (client.available()) { // tant que des octets sont disponibles en lecture
        // le test est vrai si il y a au moins 1 octet disponible
        char c = client.read(); // l'octet suivant reçu du client est mis dans la variable c
        comptChar=comptChar+1; // incrémente le compteur de caractère reçus
        Serial.print(c); // affiche le caractère reçu dans le Terminal Série
        //--- on ne mémorise que les n premiers caractères de la requete reçue
        //--- afin de ne pas surcharger la RAM et car cela suffit pour l'analyse de la requete
        if (comptChar<=100) chaineRecue=chaineRecue+c; // ajoute le caractère reçu au String pour les N premiers caractères
        //else break; // une fois le nombre de caractères dépassés sort du while
      } // --- fin while client.available = fin "tant que octet en lecture"
      
      Serial.println (F("Reception requete terminee"));
      /********* Affichage de la requete reçue ************/
      Serial.println(F("------------ Affichage de la requete recue ------------")); // affiche le String de la requete
      Serial.println (F("Chaine prise en compte pour analyse : "));
      Serial.println(chaineRecue); // affiche le String de la requete pris en compte pour analyse
      /************* Analyse de la requete reçue ****************/
      Serial.println(F("------------ Analyse de la requete recue ------------")); // analyse le String de la requete
      //------ analyse si la chaine reçue est une requete GET --------
      if (chaineRecue.startsWith("GET")) { // si la chaine recue commence par GET
        Serial.println (F("Requete HTTP valide !"));
          //----- +/- extraction et analyse de la sous-chaine utile ------

          /************ Réponse HTTP suivie de la Page HTML de réponse ****************/
        //-- envoi de la réponse HTTP ---
        client.println(F("HTTP/1.1 200 OK")); // entete de la réponse : protocole HTTP 1.1 et exécution requete réussie
        client.println(F("Content-Type: text/html")); // précise le type de contenu de la réponse qui suit
        client.println(F("Connnection: close")); // précise que la connexion se ferme après la réponse
        client.println(); // ligne blanche
          //--- envoi en copie de la réponse http sur le port série
        Serial.println(F("La reponse HTTP suivante est envoyee au client distant :"));
        Serial.println(F("HTTP/1.1 200 OK"));
        Serial.println(F("Content-Type: text/html"));
        Serial.println(F("Connnection: close"));
        Serial.println();

            //------ début de la page HTML -------

        client.println(F("<!DOCTYPE html>"));
        client.println(F("<html>"));
            //------- head = entete de la page HTML ----------
        client.println(F("<head>"));
        client.println(F("<meta charset=\"utf-8\" />")); // fixe encodage caractères - utiliser idem dans navigateur
            // client.println(F("<meta http-equiv=\"refresh\" content=\"1\" /> <!-- pour reactualisation auto toutes les x secondes -->"));
        client.println(F("<title>Plantes</title>"));// titre de la page HTM

//=============== bloc de code javascript ================
client.println(F("<!-- Début du code Javascript -->"));
client.println(F("<script language=\"javascript\" type=\"text/javascript\">"));
client.println(F("<!-- "));
client.println(F("window.onload = function () { // au chargement de la page"));
client.println(F("var canvas = document.getElementById(\"nomCanvas\"); // declare objet canvas a partir id = nom "));
client.println(F("canvas.width = 800; // largeur canvas"));
client.println(F("canvas.height = 680; // hauteur canvas"));
client.println(F("if (canvas.getContext){ // la fonction getContext() renvoie True si canvas accessible"));

//--------------> Le Code graphique <----------------------

client.println(F("var ctx = canvas.getContext(\"2d\"); // objet contexte permettant acces aux fonctions de dessin"));
client.println(F("ctx.fillStyle = \"rgb(255,253,245)\"; // couleur de remplissage = jaune clair"));
client.println(F("ctx.fillRect (0, 0, canvas.width, canvas.height); // rectangle de la taille du canva"));
client.println(F("var larg_fen = 800;"));
client.println(F(" var haut_fen = 680;"));
client.println(F("  var haut_ligne = 40;"));
client.println(F("  var haut_ligne1 = 60;"));
client.println(F("  var larg_col1 = 60;"));
client.println(F("  var larg_col2 = 200;"));
client.println(F("  var larg_col3 = 480;"));
client.println(F("  var larg_col4 = 60;"));
client.println(F("  var haut_EnTete =100;"));
client.println(F(" var val ;"));
client.println(F(" var plantes2 ;"));

// Dessin entete tableau
client.println(F("ctx.strokeStyle= \"rgb(0,0,0)\"; // couleur noire"));
client.println(F("ctx.strokeRect(0,0,larg_fen,haut_EnTete)"));
client.println(F("ctx.strokeRect(500,0,larg_fen - 500,60)"));
client.println(F("ctx.strokeRect(0,haut_EnTete,larg_fen,haut_ligne1)"));

// ligne du haut du tableau
client.println(F("ctx.strokeRect(0,haut_EnTete, larg_col1 , haut_ligne1);"));
client.println(F("ctx.strokeRect(0,haut_EnTete , larg_col2 , haut_ligne1);"));
client.println(F("ctx.strokeRect(0,haut_EnTete  ,larg_fen - larg_col4 , haut_ligne1);"));
client.println(F("ctx.strokeRect(larg_fen - larg_col4 ,haut_EnTete , larg_col4 , haut_ligne1);"));

// lignes du tableau
client.println(F("ctx.strokeRect(larg_col2,haut_EnTete + haut_ligne1 , 110 , haut_ligne * 12);")); // ok
client.println(F("for (var i=0; i<12; i=i+1) {")); // debut for
    client.println(F("ctx.strokeRect(0,haut_EnTete + haut_ligne1 + haut_ligne*i, larg_col1 , haut_ligne);")); // ok
    client.println(F("ctx.strokeRect(0,haut_EnTete + haut_ligne1 + haut_ligne*i, larg_col2 , haut_ligne);")); // ok
    client.println(F("ctx.strokeRect(0,haut_EnTete + haut_ligne1 + haut_ligne*i ,larg_fen - larg_col4 , haut_ligne);")); // ok
    client.println(F("ctx.strokeRect(larg_fen - larg_col4 ,haut_EnTete + haut_ligne1 + haut_ligne*i, larg_col4 , haut_ligne);")); // ok
client.println(F("}")); // fin for
    /* bas de tableau */
client.println(F("ctx.strokeRect(0 ,haut_EnTete + haut_ligne1 + haut_ligne*12, 800 ,40);"));
client.println(F("ctx.strokeRect(0 ,haut_EnTete + haut_ligne1 + haut_ligne*12, 200 ,40);"));

// Affichage du texte fixe (n° pot, plantes, ligne d'en tete du tableau)

client.println(F("ctx.font='20pt Arial, Calibri, Geneva';"));
client.println(F("ctx.fillStyle = 'Black';"));
client.println(F("ctx.fillText(\"ETAT DE L\' ARROSAGE\",110,60);")); // pas d'apostrohe possible !
client.println(F("ctx.font='12pt Arial, Calibri, Geneva';"));
client.println(F("ctx.fillText('Date du jour : ',16,24);"));
client.println(F("ctx.fillText(Date(),135,24);"));  
client.println(F("ctx.fillText('Dernier arrosage fait le : ',16,90);"));
    /* T et H humidité */
client.println(F("ctx.font='14pt Arial, Calibri, Geneva';"));
client.println(F("ctx.fillText('Appartement',30,670);"));
client.println(F("ctx.fillText('Température = ',250,670);"));
client.println(F("ctx.fillText('Humidité = ',550,670);"));

/* Affichage des en-tete des colonnes */
client.println(F("ctx.fillStyle = 'Black';"));
client.println(F("ctx.font='12pt Arial, Calibri, Geneva';"));
client.println(F("ctx.fillText('N° Pot',6,haut_EnTete + haut_ligne1 - 20);"));
client.println(F("ctx.font='14pt Arial, Calibri, Geneva';"));
client.println(F("ctx.fillText('Plantes',larg_col1 + 40,haut_EnTete + haut_ligne1 - 20);")); 
client.println(F("ctx.fillText('- 20%',larg_col2 + 5,haut_EnTete + haut_ligne1 - 20);")); 
client.println(F("ctx.fillText('0',larg_col2 + 105,haut_EnTete + haut_ligne1 - 20);")); 
client.println(F("ctx.fillText('Humidité relative',larg_col1 + larg_col2 + 170,haut_EnTete + haut_ligne1 - 20);")); 
client.println(F("ctx.fillText('100%',larg_col1 + larg_col2 + 420,haut_EnTete + haut_ligne1 - 20);")); 
client.println(F("ctx.fillText('Val',larg_col1 + larg_col2 + 500,haut_EnTete + haut_ligne1 - 20);")); 

/* Affichage des n° des pots */
client.println(F("for (var i=1; i<10; i=i+1) {")); // debut for pots de 1 à 9
    client.println(F("ctx.fillText(i,24,haut_EnTete+haut_ligne * (i+1)+5);")); // n° des pots
  client.println(F("}")); // fin for
client.println(F("for (var i=10; i<13; i=i+1) {")); // debut for pour centrer text pots de 10 à 12
    client.println(F("ctx.fillText(i,18,haut_EnTete+haut_ligne * (i+1)+5);")); // n° des pots
  client.println(F("}")); // fin for

/*    creation d'un tableau de 12 chaines en SRAM NE FONCTIONNE PAS : 
 *     pb de passage plantes[] à plantes2[] en mem FLASH
 *     D'après X. Hinault il faut passer par une librairie ...
*/
/* affichage nom des plantes à defaut d'autre methode plus propre ou plus simple!  */
   client.println(F("ctx.font='14pt Arial, Calibri, Geneva'; "));
   client.println(F("ctx.fillText('F. Lyrata 1',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne- 10); "));
   client.println(F("ctx.fillText('F. Lyrata 2',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*2- 10); "));
   client.println(F("ctx.fillText('Dracena 1',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*3- 10); "));
   client.println(F("ctx.fillText('Dracena 2',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*4- 10); "));
   client.println(F("ctx.fillText('Beaucarnea',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*5- 10); "));
   client.println(F("ctx.fillText('Bananier 1',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*6- 10); "));
   client.println(F("ctx.fillText('Bananier 2',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*7- 10); "));
   client.println(F("ctx.fillText('Olivier',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*8 -10); "));
   client.println(F("ctx.fillText('Laurier',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*9- 10); "));
   client.println(F("ctx.fillText('Mandarinier',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*10- 10); "));
   client.println(F("ctx.fillText('Rose Caraibe',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*11- 10); "));
   client.println(F("ctx.fillText('Ficus',larg_col1 + 15,haut_EnTete  + haut_ligne1+ haut_ligne*12- 10); "));

// Affichage de l'état de l'arrosage
Serial.print("arrosage = ");
Serial.println(arrosage);
  if (arrosage) {
    client.println(F("ctx.fillStyle = \'rgb(123,248,63)\';")); // couleur de remplissage = vert clair"));
    client.println(F("ctx.fillRect(501,1,larg_fen - 502,58);"));
    client.println(F("ctx.font='24pt Arial, Calibri, Geneva';"));
    client.println(F("ctx.fillStyle = 'Black';"));
    client.println(F("ctx.fillText('PAS DE FUITES !',520,40);"));
    client.println(F("ctx.font='18pt Arial, Calibri, Geneva';"));
    client.println(F("ctx.fillStyle = 'Blue';"));
    client.println(F("ctx.fillText('Arrosage éventuel prévu à 18h',440,90);"));
  }
    else{
    client.println(F("ctx.fillStyle = \'rgb(253,67,47)\';")); // couleur de remplissage = rouge"
    client.println(F("ctx.fillRect(501,1,larg_fen - 502,58);"));
    client.println(F("ctx.font='18pt Arial, Calibri, Geneva';"));
    client.println(F("ctx.fillStyle = 'Black';"));
    client.println(F("ctx.fillText('IL Y A DES FUITES ! ! !',520,40);"));
    client.println(F("ctx.fillStyle = 'Red';"));
    client.println(F("ctx.font='20pt Arial, Calibri, Geneva';"));
    client.println(F("ctx.fillText('ARROSAGE ARRETE !',500,90);"));
    }

/* recupere la date dernier arrosage sur la carte SD  */
/* Format : Wednesday  24.02.2016 - 16:52:09 par exemple */

  Serial.println("Initialisation de la carte SD en cours...");
  if (!SD.begin(selectSD)) { // si initialisation avec broche selectSD en tant que CS n'est pas réussie
  Serial.println("Echec initialisation!"); // message port Série
  }
  
strLine = getLine("datas.txt",1,true); // ligne 1 pour le dernier arrosage
Serial.print("strLine = "); // pour debug
Serial.println(strLine);  // ok
Serial.println(strLine.length()); // debug (33) OK
Serial.println("-----");

    for ( int i = 0;i<=strLine.length() ; i++)
    {
      temp = strLine.substring(i,i+1);
     if ((strLine.charAt(i)>=48) && (strLine.charAt(i)<=57))
        {
        Str_Num=Str_Num+temp;
        }
    } // fin boucle i
    Serial.println();

 Serial.println(" --------------------------- ");
 Serial.println(Str_Num);

/*  affichage dernier arrosage */

 date_Aros[0]= (Str_Num.substring(0,2)).toInt(); // jour
 date_Aros[1]= (Str_Num.substring(2,4)).toInt(); // mois
 date_Aros[2]= (Str_Num.substring(4,8)).toInt(); // année
 date_Aros[3]= (Str_Num.substring(8,10)).toInt(); // heure
Serial.println(date_Aros[0]);
Serial.println(date_Aros[1]);
Serial.println(date_Aros[2]);
Serial.println(date_Aros[3]);
    client.println(F("var val=[")); // tableau de 4 variables
      for (int i=0; i<4; i=i+1) {
          int val = date_Aros[i];
          client.print(val);
          if(i<3) client.print(",");
      }
  client.println(F("];")); // tableau de 4 valeurs numeriques"));
// client.println(F("ctx.fillStyle = \"rgb(255,253,245)\"; // couleur de remplissage = jaune clair"));
//client.println(F("ctx.fillRect (180, 75, 370, 92); // rectangle"));
 
  client.println(F("ctx.fillStyle = 'Blue';")); // couleur de remplissage = bleu
  client.println(F("ctx.font='16pt Arial, Calibri, Geneva';"));
  client.println(F("ctx.fillText(val[0] ,200,90);")); // jour
  client.println(F("ctx.fillText(\' - \' ,225,90);"));
  client.println(F("ctx.fillText(val[1] ,245,90);")); // mois
  client.println(F("ctx.fillText(\' - \' ,265,90);"));
  client.println(F("ctx.fillText(val[2] ,285,90);")); // année
  
  client.println(F("ctx.fillText(\' : \' ,335,90);"));
  client.println(F("ctx.fillText(val[3] ,350,90);")); // heure
  client.println(F("ctx.fillText(\' h\' ,370,90);"));
  

// Affichage du taux d'humidité
      /* creation du tableau de 12 valeurs en SRAM : OK */
    client.println(F("var val=["));
      for (int i=0; i<12; i=i+1) {
          int val = taux_Hum[i];
          client.print(val);
 //       Serial.println(val*2); // pour debug OK : val est bien un nombre !!!!
          if(i<11) client.print(",");
      }
     client.println(F("];"));  // fin tableau de 12 valeurs numeriques
   
      /* Affichage des valeurs d'humidite : OK */ 
    client.println(F("ctx.font='14pt Arial, Calibri, Geneva'; "));
 client.println(F("for (var i=0; i<12; i=i+1) {")); // debut for
    client.println(F("if ( val[i] <0) {"));
      client.println(F("ctx.fillStyle = \'rgb(247,55,21)\';")); // couleur de remplissage = rouge"
      client.println(F("ctx.fillText(val[i],larg_col1 + larg_col2 + 495,haut_EnTete+haut_ligne1+haut_ligne * (i+1) - 10);"));
      client.println(F("}"));
    client.println(F("else {"));
      client.println(F("ctx.fillStyle = 'Blue';")); // couleur de remplissage = bleu
      client.println(F("ctx.fillText(val[i],larg_col1 + larg_col2 + 500,haut_EnTete+haut_ligne1+haut_ligne * (i+1) - 10);"));
    client.println(F("}")); // fin else
  client.println(F("}")); // fin for
      
      /* tracé des donnees en vu-metre : OK */
 client.println(F("for (var i=0; i<12; i=i+1) {")); // debut for
   client.println(F("if ( val[i] > 0){"));
      client.println(F("ctx.fillStyle = \'rgb(20,168,250)\';")); // couleur de remplissage = bleu clair"));
      client.println(F("ctx.fillRect(larg_col2 + 111,haut_EnTete+haut_ligne1+haut_ligne * (i) + 1,val[i]*4,38);")); 
      client.println(F("}")); // fin if
   client.println(F("else {"));
       client.println(F("if (val[i] < -20){"));
      client.println(F("val[i] = -20 }"));
      client.println(F("ctx.fillStyle = \'rgb(247,55,21)\';")); // couleur de remplissage = rouge"
      client.println(F("ctx.fillRect(larg_col2 + 109,haut_EnTete+haut_ligne1+haut_ligne * (i) + 1,val[i]*4,38);"));
     client.println(F("}")); // fin else
  client.println(F("}")); // fin for

/* affiche température et humidité appartement */
    client.println(F("var val=["));
    int val=temp_hum[0];
    client.print(val);
    client.print(",");
    val=temp_hum[1];
    client.print(val);
    client.println(F("];"));
    client.println(F("ctx.fillStyle = \'rgb(247,55,21)\';")); // couleur de remplissage = rouge"
    client.println(F("ctx.fillText(val[0],400,670);"));
    client.println(F("ctx.fillText(' °C',430,670);"));
    client.println(F("ctx.fillStyle = \'rgb(20,168,250)\';")); // couleur de remplissage = bleu clair"));
    client.println(F("ctx.fillText(val[1],650,670);"));
    client.println(F("ctx.fillText('%',680,670);"));


/*--------------> Fin du Code graphique <---------------------- */

client.println(F("} // fin si canvas existe"));
client.println(F("else {"));
client.println(F("// code si canvas non disponible "));
client.println(F("} // fin else"));
client.println(F("} // fin onload"));
client.println(F("//-->"));
client.println(F("</script>"));
client.println(F("<!-- Fin du code Javascript --> "));
/* =============== fin du bloc de code javascript =============== */
    client.println(F("</head>"));

    //------- body = corps de la page HTML ----------
    client.println(F("<body>"));
    // affiche chaines caractères simples
    client.println(F("<CENTER>")); //pour centrer la suite de la page
    client.println(F("<canvas id=\"nomCanvas\" width=\"200\" height=\"300\"></canvas>"));
    client.println(F("<h1 style='font-size:150%;'>Humidité relative des pots ( / minimum défini pour le pot )</h1>"));
    client.println(F("</body>"));
//------- fin body = fin corps de la page ----------
  client.println(F("</html>"));
//-------- fin de la page HTML -------
} // fin if GET

  else { // si la chaine recue ne commence pas par GET
    Serial.println (F("Requete HTTP non valide !"));
  } // fin else
    //------ fermeture de la connexion ------
    // fermeture de la connexion avec le client après envoi réponse
  delay(10); // laisse le temps au client de recevoir la réponse
  client.stop();
  Serial.println(F("------------ Fermeture de la connexion avec le client ------------")); // affiche le String de la requete
  Serial.println (F(""));
} // --- fin if client connected
  } // fin fonction webserver

        /* ---- température et humidité appartement ---- */ 

void Temp_Hum_Function()
  {
  delay(dht.getMinimumSamplingPeriod());
  float humidity = dht.getHumidity();
  float temperature = dht.getTemperature();
  Serial.print("capteur température - humidité = ");
  Serial.println(dht.getStatusString());
  Serial.println("\t");
  Serial.print(temperature, 1); // 1 décimale
  Serial.print("\t\t");
  Serial.print(humidity, 1); // 1 décimale
  Serial.println("\t\t");
  // Serial.println(dht.toFahrenheit(temperature), 1);
  temp_hum[0]= int(temperature);
  temp_hum[1] = int(humidity); 
  // Serial.print("t = ");Serial.print(temp_hum[0]);Serial.print("     h = ");Serial.println(temp_hum[1]);
  }
     
  

