
/*
 ESP8266 MQTT - Relevé de température et humidité via DHT11
 Création Dominique PAUL. Modifié par Pascal Courtonne
 Dépot Github : https://github.com/DomoticDIY/MQTT-ModuleDHT11
  Chaine YouTube du Tuto Vidéo : https://www.youtube.com/c/DomoticDIY
 
 Bibliothéques nécessaires :
  - pubsubclient : https://github.com/knolleary/pubsubclient
  - ArduinoJson v5.13.3 : https://github.com/bblanchon/ArduinoJson
 Télécharger les bibliothèques, puis dans IDE : Faire Croquis / inclure une bibliothéque / ajouter la bibliothèque ZIP.
 Puis dans IDE : Faire Croquis / inclure une bibliothéque / Gérer les bibliothèques, et ajouter :
  - DHT Sensor Library by AdaFruit
  - AdaFruit Unified sensor by AdaFruit
 
 Dans le gestionnaire de bibliothéque, charger le module ESP8266Wifi.
 Installer le gestionnaire de carte ESP8266 version 2.5.0 
 Si besoin : URL à ajouter pour le Bord manager : http://arduino.esp8266.com/stable/package_esp8266com_index.json
 
 Pour prise en compte du matériel :
 Installer si besoin le Driver USB CH340G : https://wiki.wemos.cc/downloads
 dans Outils -> Type de carte : generic ESP8266 module
  Flash mode 'QIO' (régle générale, suivant votre ESP, si cela ne fonctionne pas, tester un autre mode.
  Flash size : 1M (no SPIFFS)
  Port : Le port COM de votre ESP vu par windows dans le gestionnaire de périphériques.
*/

// Inclure les librairies.
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// =================================================================================================

// ---------------------------------------
// Définitions liées au module capteur DHT
// ---------------------------------------

String nomModule = "Température & Humidité";          // Nom usuel de ce module. Sera visible uniquement dans les Log Domoticz.

#define DHTTYPE DHT11                                 // DHT 11 // DHT 22 (AM2302) // DHT 21 (AM2301)
#define DHTPIN  2
#define tempsPause 30                                 // Nbre de secondes entre deux mesures

// ------------------------------------
// Définitions liées à Domoticz et MQTT
// ------------------------------------

char*       topicIn =   "domoticz/out";      // Nom du topic envoyé par Domoticz
char*       topicOut =  "domoticz/in";       // Nom du topic écouté par Domoticz
int         idxDevice = 42;                  // Index du device dans Domoticz.
const char* mqtt_server = "MQTT_BROKER_IP";  // Adresse IP ou DNS du Broker MQTT.
const int   mqtt_port = 1883;                // Port du Brocker MQTT
const char* mqtt_login = "MQTT_LOGIN";       // Login de connexion à MQTT.
const char* mqtt_password = "MQTT_PASSWD";   // Mot de passe de connexion à MQTT.

// -------------------------------------------------------------
// Définitions liées au WIFI
// -------------------------------------------------------------
const char* ssid = "WIFI_SSID";              // SSID du réseau Wifi
const char* password = "WIFI_PASSWD";        // Mot de passe du réseau Wifi.

// ------------------------------------------------------------
// Variables globales
// ------------------------------------------------------------

float valHum = 0.0;                         // Variables contenant la valeur de l'humidité.
float valTemp = 0.0;                        // Variables contenant la valeur de température.
float valHum_T, valTemp_T;                  // Valeurs de relevé temporaires.

// =================================================================================================

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE, 11);

// SETUP
// *****
void setup() {
  Serial.begin(115200);                       // On initialise la vitesse de transmission de la console.
  
  dht.begin();                                // On initialise le DHT sensor

  client.setBufferSize(512);
  client.setServer(mqtt_server, mqtt_port);   // On défini la connexion MQTT
}

// BOUCLE DE TRAVAIL
// *****************
void loop() {

  while (WiFi.status() != WL_CONNECTED)
    setup_wifi();
    
  if (!client.connected()) {
    // Serial.println("Connexion au serveur MQTT");
    reconnect();
  } else {    
    getTempHum();             // On interroge la sonde de Température / Humidité.
    SendData();               // Envoi de la données via JSON et MQTT
    Serial.printf("Pause de %d secondes\n", tempsPause);
    delay(tempsPause * 1000); // On met le système en pause pour un temps défini
  }
}

// CONNEXION WIFI
// **************
void setup_wifi() {
  
  // Connexion au réseau Wifi
  // delay(10);
  Serial.println();
  Serial.print("Connexion au point d'accès Wifi '");
  Serial.print(ssid);
  Serial.println("'");

  WiFi.mode(WIFI_STA);
  // Serial.printf("Wi-Fi mode réglé sur WIFI_STA %s\n", WiFi.mode(WIFI_STA) ? "" : "Echec!");
  WiFi.begin(ssid, password);

  int wifi_status = WiFi.status();
  while ( wifi_status != WL_CONNECTED) {
    // Serial.printf("Connection status: %d\n", wifi_status);
    if( wifi_status == WL_NO_SSID_AVAIL) {
      Serial.println("");
      Serial.println("*** Point d'accès Wifi inaccessible");
      return;
    }
    if( wifi_status == WL_CONNECT_FAILED) {
      Serial.println("");
      Serial.println("*** Echec de la connexion Wifi");
      return;
    }
    /*
    if( wifi_status == WL_CONNECT_WRONG_PASSWORD) {
      Serial.println("");
      Serial.print("Wifi password is incorrect");
      return;
    }
    */
    // Tant que l'on est pas connecté, on boucle.
    delay(500);
    Serial.print(".");
    wifi_status = WiFi.status();
  }

  Serial.println("");
  Serial.println("WiFi connecté");
  Serial.print("Addresse IP : ");
  Serial.println(WiFi.localIP());
}

// CONNEXION MQTT
// **************
void reconnect() {

  // Initialise la séquence Random
  randomSeed(micros());
  
  Serial.print("Connexion au serveur MQTT...");
  // Création d'un ID client aléatoire
  String clientId = "TempIOT-";
  clientId += String(random(0xffff), HEX);
    
  // Tentative de connexion
  if (client.connect(clientId.c_str(), mqtt_login, mqtt_password)) {
    Serial.println("connecté");
      
    // Connexion effectuée, publication d'un message...
    String message = "Connexion MQTT de "+ nomModule + " (ID : " + clientId + ")";
    char messageChar[message.length()+1];
    message.toCharArray(messageChar,message.length()+1);
      
    const int capacity = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<capacity> root;
 
    // On renseigne les variables.
    root["command"] = "addlogmessage";
    root["message"] = (const char *) messageChar;
      
    // On sérialise la variable JSON
    String JsonStr;
    serializeJson(root, JsonStr);

    // Convertion du message en Char pour envoi dans les Log Domoticz.
    char JsonStrChar[JsonStr.length()+1];
    JsonStr.toCharArray(JsonStrChar,JsonStr.length()+1);
    client.publish(topicOut, JsonStrChar);

    // On souscrit
    client.subscribe("#");
    } else {
      Serial.print("Erreur, rc=");
      Serial.print(client.state());
      Serial.println(" prochaine tentative dans 5s");
      // Pause de 5 secondes
      delay(5000);
    }
}

// RELEVE DE TEMPERATURE ET HUMIDITE.
// **********************************
void getTempHum() {
  
  valTemp_T = dht.readTemperature();        // Lecture de la température : (Celcius par défaut, "dht.readTemperature(true)" = Fahrenheit)
  
  // On vérifie que le relevé est valide.
  if (isnan(valTemp_T)) {
    // La valeur retournée n'es pas valide (isnan = is Not A Number).
    Serial.println("*** Erreur lors du relevé de la température");
  } else {
    Serial.print("Valeur de température relevée : ");
    Serial.println(valTemp_T);
    valTemp = valTemp_T;
  }
  
  // On relève l'humidité.
  // --------------------
  valHum_T = dht.readHumidity();            // Lecture de l'humidité (en %)
  if (isnan(valHum_T)) {
  // La valeur retournée n'es pas valide (isnan = is Not A Number).
    Serial.println("*** Erreur lors du relevé de l'humidité");
  } else {
    Serial.print("Valeur d'humidité relevée : ");
    Serial.println(valHum_T);
    valHum = valHum_T;
  }
}

// Envoi de la donnée MQTT au format JSON
// **************************************
void SendData () {

  String svalue = String(valTemp)+";"+String(valHum)+";0"; // txtValRetour
  char svalueChar[svalue.length()+1];
  svalue.toCharArray(svalueChar,svalue.length()+1);
      
  const int capacity = JSON_OBJECT_SIZE(5);
  StaticJsonDocument<capacity> root;

  // On renseigne les variables.
  root["type"]    = "command";
  root["param"]   = "udevice";
  root["idx"]     = idxDevice;
  root["nvalue"]  = 0;
  root["svalue"]  = (const char *) svalueChar;
      
  // On sérialise les données au format JSON

  String JsonStr;
  serializeJson(root, JsonStr);
  
  // Serial.println(JsonStr);
  
  char JsonStrChar[JsonStr.length()+1];
  JsonStr.toCharArray(JsonStrChar,JsonStr.length()+1);
  client.publish(topicOut, JsonStrChar);
  
  Serial.print("Message envoyé : ");
  Serial.println(JsonStr);
}
