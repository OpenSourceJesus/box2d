using UnityEngine;
public class Ball : MonoBehaviour {
    public int enters;
    public int stays;
    public int exits;
    void OnCollisionEnter2D(Collision2D coll) {
        enters = enters + 1;
        if (enters < 4) {
            GetComponent<Rigidbody2D>().velocity = new Vector2(0, 6);
        }
    }
    void OnCollisionStay2D(Collision2D coll) {
        stays = stays + 1;
    }
    void OnCollisionExit2D(Collision2D coll) {
        exits = exits + 1;
    }
}
