import argparse
import os
import json
import numpy as np
import tensorflow as tf
import open3d

import model
from dataset.semantic_dataset import SemanticDataset
from util.metric import ConfusionMatrix


class Predictor:
    def __init__(self, checkpoint_path, hyper_params, batch_size):
        # Get ops from graph
        with tf.device("/gpu:0"):
            # Placeholder
            pl_points, _, _ = model.get_placeholders(
                batch_size, hyper_params["num_point"], hyperparams=hyper_params
            )
            pl_is_training = tf.placeholder(tf.bool, shape=())
            print("pl_points shape", tf.shape(pl_points))

            # Prediction
            pred, _ = model.get_model(
                pl_points, pl_is_training, dataset.num_classes, hyperparams=hyper_params
            )

            # Saver
            saver = tf.train.Saver()

        self.ops = {
            "pl_points": pl_points,
            "pl_is_training": pl_is_training,
            "pred": pred,
        }

        # Restore checkpoint to session
        config = tf.ConfigProto()
        config.gpu_options.allow_growth = True
        config.allow_soft_placement = True
        config.log_device_placement = False
        self.sess = tf.Session(config=config)
        saver.restore(self.sess, checkpoint_path)
        print("Model restored")

    def predict(self, batch_data):
        """
        Args:
            batch_data: batch_size * num_point * 6(3)

        Returns:
            pred_labels: batch_size * num_point * 1
        """
        is_training = False
        feed_dict = {
            self.ops["pl_points"]: batch_data,
            self.ops["pl_is_training"]: is_training,
        }
        pred_val = self.sess.run([self.ops["pred"]], feed_dict=feed_dict)
        pred_val = pred_val[0]  # batch_size * num_point * 1
        pred_labels = np.argmax(pred_val, 2)  # batch_size * num_point * 1
        return pred_labels


if __name__ == "__main__":
    np.random.seed(0)

    # Parser
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--num_samples",
        type=int,
        default=8,
        help="# samples, each contains num_point points",
    )
    parser.add_argument("--ckpt", default="", help="Checkpoint file")
    parser.add_argument("--set", default="validation", help="train, validation, test")
    flags = parser.parse_args()
    hyper_params = json.loads(open("semantic.json").read())

    # Create output dir
    output_dir = os.path.join("result", "sparse")
    os.makedirs(output_dir, exist_ok=True)

    # Dataset
    dataset = SemanticDataset(
        num_points_per_sample=hyper_params["num_point"],
        split=flags.set,
        box_size=hyper_params["box_size"],
        use_color=hyper_params["use_color"],
        path=hyper_params["data_path"],
    )

    # Model
    batch_size = 4
    predictor = Predictor(
        checkpoint_path=flags.ckpt, hyper_params=hyper_params, batch_size=batch_size
    )

    # Process each file
    cm = ConfusionMatrix(9)

    for semantic_file_data in dataset.list_file_data[:1]:
        print("Processing {}".format(semantic_file_data))

        # Predict for num_samples times
        points_raw_collector = []
        pd_labels_collector = []

        # TODO: check "flags.num_samples / batch_size"
        for _ in range(int(flags.num_samples / batch_size)):
            # Get data
            points, points_raw, gt_labels, colors = semantic_file_data.sample_batch(
                batch_size=batch_size, num_points_per_sample=hyper_params["num_point"]
            )

            # (bs, 8192, 3) concat (bs, 8192, 3) -> (bs, 8192, 6)
            points_with_colors = np.concatenate((points, colors), axis=-1)

            # Predict
            pd_labels = predictor.predict(points_with_colors)

            # Save to collector for file output
            points_raw_collector.extend(points_raw)
            pd_labels_collector.extend(pd_labels)

            # Increment confusion matrix
            cm.increment_from_list(gt_labels.flatten(), pd_labels.flatten())

        # Save sparse point cloud and predicted labels
        file_prefix = os.path.basename(semantic_file_data.file_path_without_ext)

        points_raw_collector = np.array(points_raw_collector)
        pcd = open3d.PointCloud()
        pcd.points = open3d.Vector3dVector(points_raw_collector.reshape((-1, 3)))
        pcd_path = os.path.join(output_dir, file_prefix + ".pcd")
        open3d.write_point_cloud(pcd_path, pcd)
        print("Exported pcd to {}".format(pcd_path))

        pd_labels_collector = np.array(pd_labels_collector).astype(int)
        pd_labels_path = os.path.join(output_dir, file_prefix + ".labels")
        np.savetxt(pd_labels_path, pd_labels_collector.flatten(), fmt="%d")
        print("Exported labels to {}".format(pd_labels_path))

    cm.print_metrics()
