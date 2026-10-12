# Verificación del issue #2

Necesitas una compilación actualizada, un bastión SSH y un servidor destino
accesible desde el bastión. Pueden ser máquinas virtuales o contenedores.
Para comprobar que las credenciales son independientes, usa contraseñas
distintas en ambos servidores. Habilita SFTP en el destino.

En el bastión, permite `AllowTcpForwarding yes` y comprueba que `PermitOpen`
permita el host y puerto de destino. Si modificas sshd, valida su configuración
con `sshd -t` antes de recargarlo. Para probar contraseñas, los servidores deben
permitir `PasswordAuthentication yes`.

1. Comprueba la ruta con OpenSSH:
   `ssh -J usuario_bastion@bastion:22 usuario_destino@destino`.
   El nombre del destino debe resolverse desde el bastión.
2. En BanchoXterm, crea una sesión con host, puerto y usuario de destino;
   configura Jump host, Jump port y Jump user con los datos del bastión.
3. Prueba con llaves válidas para ambos saltos. Debe abrirse la terminal sin
   errores de banner. Ejecuta `hostname` para confirmar el destino.
4. Prueba con contraseñas distintas: guarda la del destino en su campo y la
   del bastión en Jump password, marcando su casilla de guardado. Reinicia la
   aplicación y conecta de nuevo. La contraseña del bastión se almacena bajo
   una entrada independiente del gestor de credenciales, no en el JSON.
5. Desmarca el guardado del bastión y guarda la sesión. Al conectar sin llave
   ni agente válido, debe solicitar credenciales del bastión. Cancelar debe
   dejar la conexión cerrada; una contraseña incorrecta debe dar un error de
   autenticación, sin reutilizarla como contraseña del destino.
6. Prueba llave en bastión y contraseña en destino, y la combinación inversa.
7. Abre SFTP, lista archivos, sube y descarga uno y compara su contenido.
   Prueba también una transferencia paralela y reconectar la sesión.
8. Verifica que una sesión SSH directa sigue conectando.

Para reutilizar una sesión, crea primero una sesión SSH directa del bastión
y guarda su llave o contraseña. En la sesión del destino, selecciona ese
perfil en «Usar sesión SSH guardada». Sus datos se muestran sin permitir
edición y la contraseña se lee de la entrada original del gestor de
credenciales. Cambia después el host o la contraseña del perfil del bastión
y reconecta el destino para comprobar que usa los datos actualizados.
Si eliminas el perfil del bastión, la conexión debe fallar sin intentar una
conexión directa. El selector excluye la propia sesión y los perfiles que ya
usan un bastión; no admite cadenas de saltos.

En Linux necesitas un gestor Secret Service activo y `secret-tool` (o el
proveedor KWallet compatible). En Windows se usa el gestor de credenciales.
Si el destino no es accesible o el bastión prohíbe forwarding, debe fallar al
abrir el canal hacia el destino.

Pruebas automatizadas: compila `banchoxterm_tests` y ejecuta `ctest
--test-dir <directorio-build> --output-on-failure`. La prueba
`testSshAndSftpAgainstLocalServer` incluye una conexión anidada por bastión
y un listado SFTP. Necesita Linux/Unix, `sshd`, `ssh-keygen`, `ssh-keyscan` y
un usuario no root; se omite en Windows. No sustituye las pruebas manuales
de contraseñas distintas ni de persistencia de credenciales.
